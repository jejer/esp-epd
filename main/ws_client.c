#include <stdio.h>
#include <sys/time.h>

#include "ws_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"

#ifdef WS_CLIENT_ENABLE_WSS
#include "esp_transport_ssl.h"
#endif

/* using uri parser */
#include "http_parser.h"

#define WS_MEM_CHECK(TAG, a, action) if (!(a)) {                                                      \
        ESP_LOGE(TAG,"%s:%d (%s): %s", __FILE__, __LINE__, __FUNCTION__, "Memory exhausted");       \
        action;                                                                                         \
        }

static const char *TAG = "WS_CLIENT";

typedef struct ws_data
{
    uint8_t *in_buffer;
    int in_buffer_length;
} ws_data_t;

typedef struct {
    char *uri;
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
    int network_timeout_ms;
} ws_connection_info_t;

typedef enum {
    WS_STATE_ERROR = -1,
    WS_STATE_UNKNOWN = 0,
    WS_STATE_INIT,
    WS_STATE_CONNECTED,
    WS_STATE_WAIT_TIMEOUT,
} ws_client_state_t;

struct ws_client {
    esp_transport_list_handle_t transport_list;
    esp_transport_handle_t transport;
    ws_connection_info_t connection_info;
    ws_data_t  ws_data;
    ws_client_state_t state;
    long long reconnect_tick;
    int wait_timeout_ms;
    void *user_context;
    ws_event_t event;
    ws_event_callback_t event_handle;
    int task_stack;
    int task_prio;
    bool run;
    EventGroupHandle_t status_bits;
};

const static int STOPPED_BIT = BIT0;

static long long platform_tick_get_ms();
static esp_err_t ws_dispatch_event(ws_client_handle_t client);
static esp_err_t ws_set_config(ws_client_handle_t client, const ws_client_config_t *config);
static esp_err_t ws_destroy_config(ws_client_handle_t client);
static esp_err_t ws_abort_connection(ws_client_handle_t client);
static char *create_string(const char *ptr, int len);

static esp_err_t ws_set_config(ws_client_handle_t client, const ws_client_config_t *config)
{
    //Copy user configurations to client context
    esp_err_t err = ESP_OK;

    client->task_prio = config->task_prio;
    if (client->task_prio <= 0) {
        client->task_prio = WS_TASK_PRIORITY;
    }

    client->task_stack = config->task_stack;
    if (client->task_stack == 0) {
        client->task_stack = WS_TASK_STACK;
    }
    err = ESP_ERR_NO_MEM;
    if (config->host) {
        client->connection_info.host = strdup(config->host);
        WS_MEM_CHECK(TAG, client->connection_info.host, goto _ws_set_config_failed);
    }
    client->connection_info.port = config->port;

    if (config->uri) {
        client->connection_info.uri = strdup(config->uri);
        WS_MEM_CHECK(TAG, client->connection_info.uri, goto _ws_set_config_failed);
    }

    client->connection_info.network_timeout_ms = WS_NETWORK_TIMEOUT_MS;
    client->user_context = config->user_context;
    client->event_handle = config->event_handle;
    client->connection_info.auto_reconnect = true;
    if (config->disable_auto_reconnect) {
        client->connection_info.auto_reconnect = false;
    }


    return err;
_ws_set_config_failed:
    ws_destroy_config(client);
    return err;
}

static esp_err_t ws_destroy_config(ws_client_handle_t client)
{
    free(client->connection_info.host);
    free(client->connection_info.path);
    free(client->connection_info.scheme);
    return ESP_OK;
}

static esp_err_t ws_abort_connection(ws_client_handle_t client)
{
    esp_transport_close(client->transport);
    client->wait_timeout_ms = WS_RECONNECT_TIMEOUT_MS;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = WS_STATE_WAIT_TIMEOUT;
    ESP_LOGI(TAG, "Reconnect after %d ms", client->wait_timeout_ms);
    client->event.event_id = WS_EVENT_DISCONNECTED;
    ws_dispatch_event(client);
    return ESP_OK;
}

ws_client_handle_t ws_client_init(const ws_client_config_t *config)
{
    ws_client_handle_t client = calloc(1, sizeof(struct ws_client));
    WS_MEM_CHECK(TAG, client, return NULL);

    ws_set_config(client, config);

    client->transport_list = esp_transport_list_init();
    WS_MEM_CHECK(TAG, client->transport_list, goto _ws_init_failed);

    esp_transport_handle_t tcp = esp_transport_tcp_init();
    WS_MEM_CHECK(TAG, tcp, goto _ws_init_failed);

#if WS_CLIENT_ENABLE_WSS
    esp_transport_handle_t ssl = esp_transport_ssl_init();
    WS_MEM_CHECK(TAG, ssl, goto _ws_init_failed);
    if (config->cert_pem) {
        esp_transport_ssl_set_cert_data(ssl, config->cert_pem, strlen(config->cert_pem));
    }
    if (config->client_cert_pem) {
        esp_transport_ssl_set_client_cert_data(ssl, config->client_cert_pem, strlen(config->client_cert_pem));
    }
    if (config->client_key_pem) {
        esp_transport_ssl_set_client_key_data(ssl, config->client_key_pem, strlen(config->client_key_pem));
    }
#endif

    esp_transport_handle_t ws = esp_transport_ws_init(tcp);
    WS_MEM_CHECK(TAG, ws, goto _ws_init_failed);
    esp_transport_set_default_port(ws, WS_DEFAULT_PORT);
    esp_transport_list_add(client->transport_list, ws, "ws");
    if (config->transport == WS_TRANSPORT_OVER_TCP) {
        client->connection_info.scheme = create_string("ws", 2);
        WS_MEM_CHECK(TAG, client->connection_info.scheme, goto _ws_init_failed);
    }

#if WS_CLIENT_ENABLE_WSS
    esp_transport_handle_t wss = esp_transport_ws_init(ssl);
    WS_MEM_CHECK(TAG, wss, goto _ws_init_failed);
    esp_transport_set_default_port(wss, WSS_DEFAULT_PORT);
    esp_transport_list_add(client->transport_list, wss, "wss");
    if (config->transport == WS_TRANSPORT_OVER_SSL) {
        client->connection_info.scheme = create_string("wss", 3);
        WS_MEM_CHECK(TAG, client->connection_info.scheme, goto _ws_init_failed);
    }
#endif

    if (client->connection_info.uri) {
        if (ws_client_set_uri(client, client->connection_info.uri) != ESP_OK) {
            goto _ws_init_failed;
        }
    }

    if (client->connection_info.scheme == NULL) {
        client->connection_info.scheme = create_string("ws", 2);
        WS_MEM_CHECK(TAG, client->connection_info.scheme, goto _ws_init_failed);
    }

    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = WS_BUFFER_SIZE_BYTE;
    }

    client->ws_data.in_buffer = (uint8_t *)malloc(buffer_size);
    WS_MEM_CHECK(TAG, client->ws_data.in_buffer, goto _ws_init_failed);
    client->ws_data.in_buffer_length = buffer_size;

    client->status_bits = xEventGroupCreate();
    WS_MEM_CHECK(TAG, client->status_bits, goto _ws_init_failed);
    return client;
_ws_init_failed:
    ws_client_destroy(client);
    return NULL;
}

esp_err_t ws_client_destroy(ws_client_handle_t client)
{
    ws_client_stop(client);
    ws_destroy_config(client);
    esp_transport_list_destroy(client->transport_list);
    vEventGroupDelete(client->status_bits);
    free(client->ws_data.in_buffer);
    free(client);
    return ESP_OK;
}

static char *create_string(const char *ptr, int len)
{
    char *ret;
    if (len <= 0) {
        return NULL;
    }
    ret = calloc(1, len + 1);
    WS_MEM_CHECK(TAG, ret, return NULL);
    memcpy(ret, ptr, len);
    return ret;
}

esp_err_t ws_client_set_uri(ws_client_handle_t client, const char *uri)
{
    struct http_parser_url puri;
    http_parser_url_init(&puri);
    int parser_status = http_parser_parse_url(uri, strlen(uri), 0, &puri);
    if (parser_status != 0) {
        ESP_LOGE(TAG, "Error parse uri = %s", uri);
        return ESP_FAIL;
    }

    if (client->connection_info.scheme == NULL) {
        client->connection_info.scheme = create_string(uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    }

    if (client->connection_info.host == NULL) {
        client->connection_info.host = create_string(uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    }

    if (client->connection_info.path == NULL) {
        client->connection_info.path = create_string(uri + puri.field_data[UF_PATH].off, puri.field_data[UF_PATH].len);
    }
    if (client->connection_info.path) {
        esp_transport_handle_t trans = esp_transport_list_get_transport(client->transport_list, "ws");
        if (trans) {
            esp_transport_ws_set_path(trans, client->connection_info.path);
        }
        trans = esp_transport_list_get_transport(client->transport_list, "wss");
        if (trans) {
            esp_transport_ws_set_path(trans, client->connection_info.path);
        }
    }

    if (puri.field_data[UF_PORT].len) {
        client->connection_info.port = strtol((const char*)(uri + puri.field_data[UF_PORT].off), NULL, 10);
    }

    return ESP_OK;
}

esp_err_t ws_client_write_data(ws_client_handle_t client, const char *buff, int len)
{
    int write_len = esp_transport_write(client->transport, buff, len, client->connection_info.network_timeout_ms);
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write data or timeout, written len = %d", write_len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t ws_dispatch_event(ws_client_handle_t client)
{
    client->event.user_context = client->user_context;
    client->event.client = client;

    if (client->event_handle) {
        return client->event_handle(&client->event);
    }
    return ESP_FAIL;
}


typedef struct {
    char *path;
    char *buffer;
    esp_transport_handle_t parent;
} transport_ws_t;

static esp_err_t ws_process_receive(ws_client_handle_t client)
{
    int read_len = esp_transport_read(client->transport, (char *)client->ws_data.in_buffer, client->ws_data.in_buffer_length, 5000);

    if (read_len < 0) {
        ESP_LOGE(TAG, "Read error or end of stream");
        return ESP_FAIL;
    }

    if (read_len == 0) {
        return ESP_OK;
    }

    client->event.event_id = WS_EVENT_DATA;
    client->event.data = client->ws_data.in_buffer;
    client->event.data_len = read_len;
    ws_dispatch_event(client);
    memset(client->ws_data.in_buffer, 0, client->ws_data.in_buffer_length);

    return ESP_OK;
}

static long long platform_tick_get_ms()
{
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

static void ws_task(void *pv)
{
    ws_client_handle_t client = (ws_client_handle_t) pv;
    client->run = true;

    //get transport by scheme
    client->transport = esp_transport_list_get_transport(client->transport_list, client->connection_info.scheme);

    if (client->transport == NULL) {
        ESP_LOGE(TAG, "There are no transports valid, stop ws client, scheme = %s", client->connection_info.scheme);
        client->run = false;
    }
    //default port
    if (client->connection_info.port == 0) {
        client->connection_info.port = esp_transport_get_default_port(client->transport);
    }

    client->state = WS_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    while (client->run) {

        switch ((int)client->state) {
            case WS_STATE_INIT:
                if (client->transport == NULL) {
                    ESP_LOGE(TAG, "There are no transport");
                    client->run = false;
                }

                if (esp_transport_connect(client->transport,
                                      client->connection_info.host,
                                      client->connection_info.port,
                                      client->connection_info.network_timeout_ms) < 0) {
                    ESP_LOGE(TAG, "Error transport connect");
                    ws_abort_connection(client);
                    break;
                }
                ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->connection_info.scheme, client->connection_info.host, client->connection_info.port);

                client->event.event_id = WS_EVENT_CONNECTED;
                client->state = WS_STATE_CONNECTED;
                ws_dispatch_event(client);

                break;
            case WS_STATE_CONNECTED:
                // receive and process data
                if (ws_process_receive(client) == ESP_FAIL) {
                    ws_abort_connection(client);
                    break;
                }

                break;
            case WS_STATE_WAIT_TIMEOUT:

                if (!client->connection_info.auto_reconnect) {
                    client->run = false;
                    break;
                }
                if (platform_tick_get_ms() - client->reconnect_tick > client->wait_timeout_ms) {
                    client->state = WS_STATE_INIT;
                    client->reconnect_tick = platform_tick_get_ms();
                    ESP_LOGD(TAG, "Reconnecting...");
                }
                vTaskDelay(client->wait_timeout_ms / 2 / portTICK_RATE_MS);
                break;
        }
    }
    esp_transport_close(client->transport);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);

    vTaskDelete(NULL);
}

esp_err_t ws_client_start(ws_client_handle_t client)
{
    if (client->state >= WS_STATE_INIT) {
        ESP_LOGE(TAG, "Client has started");
        return ESP_FAIL;
    }

    if (xTaskCreate(ws_task, "ws_task", client->task_stack, client, client->task_prio, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create ws task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ws_client_stop(ws_client_handle_t client)
{
    if (client->run) {
        client->run = false;
        xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
        client->state = WS_STATE_UNKNOWN;
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Client asked to stop, but was not started");
        return ESP_FAIL;
    }
}

