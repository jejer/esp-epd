#include "ws_client.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/random.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include "esp_transport.h"
#include "esp_transport_tcp.h"
#ifdef WS_CLIENT_ENABLE_WSS
#include "esp_transport_ssl.h"
#endif

/* using uri parser */
#include "http_parser.h"

typedef struct ws_data
{
    ws_header_t ws_header;
    uint8_t *rcv_buff;
    int rcv_buff_len;
    uint8_t *out_buff;
    int out_buff_len;
    int out_buff_cur;
} ws_data_t;

typedef struct {
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
    int network_timeout_ms;
    esp_transport_list_handle_t transport_list;
    esp_transport_handle_t transport;
} ws_connection_info_t;

typedef enum {
    WS_STATE_ERROR = -1,
    WS_STATE_UNKNOWN = 0,
    WS_STATE_INIT,
    WS_STATE_CONNECTED,
    WS_STATE_WAIT_TIMEOUT,
} ws_client_state_t;

typedef struct ws_client {
    ws_connection_info_t connection_info;
    ws_data_t  ws_data;
    ws_client_state_t state;
    void *user_context;
    ws_event_t event;
    ws_event_callback_t event_handle;
    int task_stack;
    int task_prio;
    bool run;
    EventGroupHandle_t status_bits;
} ws_client_t;
const static int STOPPED_BIT = BIT0;
static const char *TAG = "WS_CLIENT";

#define WS_TASK_PRIORITY            5
#define WS_TASK_STACK               (4*1024)
#define WS_NETWORK_TIMEOUT_MS       (10*1000)
#define WS_RECONNECT_TIMEOUT_MS     (10*1000)
#define WS_BUFFER_SIZE_BYTE         5*1024
#define WS_DEFAULT_PORT             80
#define WSS_DEFAULT_PORT            443

#define WS_MEM_CHECK(TAG, a, action) if (!(a)) {                                              \
        ESP_LOGE(TAG,"%s:%d (%s): %s", __FILE__, __LINE__, __FUNCTION__, "Memory exhausted"); \
        action;                                                                               \
        }

static char *create_string(const char *ptr, int len);
static esp_err_t ws_abort_connection(ws_client_handle_t client);
static esp_err_t ws_dispatch_event(ws_client_handle_t client);
static esp_err_t ws_connect(ws_client_handle_t client);
static esp_err_t ws_process_receive_frame(ws_client_handle_t client);
static char *get_http_header(const char *buffer, const char *key);
static char *trimwhitespace(const char *str);
static void ws_task(void *pv);

ws_client_handle_t ws_client_init(const ws_client_config_t *config) {
    ws_client_handle_t client = calloc(1, sizeof(ws_client_t));
    WS_MEM_CHECK(TAG, client, return NULL);

    // set configs
    client->state = WS_STATE_UNKNOWN;
    client->event_handle = config->event_handle;
    client->user_context = config->user_context;
    client->task_prio = config->task_prio;
    if (client->task_prio <= 0) {
        client->task_prio = WS_TASK_PRIORITY;
    }
    client->task_stack = config->task_stack;
    if (client->task_stack == 0) {
        client->task_stack = WS_TASK_STACK;
    }
    client->connection_info.auto_reconnect = true;
    if (config->disable_auto_reconnect) {
        client->connection_info.auto_reconnect = false;
    }
    client->connection_info.network_timeout_ms = WS_NETWORK_TIMEOUT_MS;

    struct http_parser_url puri;
    http_parser_url_init(&puri);
    if (http_parser_parse_url(config->uri, strlen(config->uri), 0, &puri) != 0) {
        ESP_LOGE(TAG, "Error parse uri = %s", config->uri);
        return NULL;
    }
    client->connection_info.scheme = create_string(config->uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    client->connection_info.host = create_string(config->uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    client->connection_info.path = create_string(config->uri + puri.field_data[UF_PATH].off, puri.field_data[UF_PATH].len);
    if (puri.field_data[UF_PORT].len) {
        client->connection_info.port = strtol((const char*)(config->uri + puri.field_data[UF_PORT].off), NULL, 10);
    }

    client->connection_info.transport_list = esp_transport_list_init();
    WS_MEM_CHECK(TAG, client->connection_info.transport_list, goto _ws_init_failed);

    if (strncmp(client->connection_info.scheme, "wss", 3) == 0) {
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
        esp_transport_list_add(client->connection_info.transport_list, ssl, "ssl");
        esp_transport_set_default_port(ssl, WSS_DEFAULT_PORT);
        client->connection_info.transport = ssl;
        if (client->connection_info.port == 0) {
            client->connection_info.port = WSS_DEFAULT_PORT;
        }
#endif
    } else {
        esp_transport_handle_t tcp = esp_transport_tcp_init();
        WS_MEM_CHECK(TAG, tcp, goto _ws_init_failed);
        esp_transport_list_add(client->connection_info.transport_list, tcp, "tcp");
        esp_transport_set_default_port(tcp, WS_DEFAULT_PORT);
        client->connection_info.transport = tcp;
        if (client->connection_info.port == 0) {
            client->connection_info.port = WS_DEFAULT_PORT;
        }
    }

    // init buffers
    client->ws_data.out_buff = config->out_buffer;
    client->ws_data.out_buff_len = config->out_buffer_size;
    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = WS_BUFFER_SIZE_BYTE;
    }
    client->ws_data.rcv_buff = (uint8_t *)malloc(buffer_size);
    WS_MEM_CHECK(TAG, client->ws_data.rcv_buff, goto _ws_init_failed);
    client->ws_data.rcv_buff_len = buffer_size;

    client->status_bits = xEventGroupCreate();
    WS_MEM_CHECK(TAG, client->status_bits, goto _ws_init_failed);
    return client;
_ws_init_failed:
    ws_client_destroy(client);
    return NULL;
}

esp_err_t ws_client_start(ws_client_handle_t client) {
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

static void ws_task(void *pv) {
    ws_client_handle_t client = (ws_client_handle_t)pv;
    client->run = true;
    client->state = WS_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    while (client->run) {

        switch ((int)client->state) {
            case WS_STATE_INIT:
                if (client->connection_info.transport == NULL) {
                    ESP_LOGE(TAG, "There are no transport");
                    client->run = false;
                    break;
                }

                if (ws_connect(client) < 0) {
                    ESP_LOGE(TAG, "Error ws_connect");
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
                if (ws_process_receive_frame(client) == ESP_FAIL) {
                    ws_abort_connection(client);
                    break;
                }

                break;
            case WS_STATE_WAIT_TIMEOUT:
                if (!client->connection_info.auto_reconnect) {
                    client->run = false;
                    break;
                }

                vTaskDelay(client->connection_info.network_timeout_ms / portTICK_RATE_MS);
                client->state = WS_STATE_INIT;
                ESP_LOGD(TAG, "Reconnecting...");
                break;
        }
    }
    esp_transport_close(client->connection_info.transport);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);

    vTaskDelete(NULL);
}

void print_bytes(void *ptr, int size) 
{
    unsigned char *p = ptr;
    int i;
    for (i=0; i<size; i++) {
        printf("%02hhX ", p[i]);
    }
    printf("\n");
}

static esp_err_t ws_process_receive_frame(ws_client_handle_t client)
{
    int received_header_len = 0;   // total received header len
    int rlen = 0;       // esp_transport_read() returned len
    int real_head_len = 2;  // real header len for this frame. <= 10 sizeof(ws_header_t)
    int read_tries = 0;

    // wait for data
    int poll_read = esp_transport_poll_read(client->connection_info.transport, client->connection_info.network_timeout_ms);
    if ( poll_read < 0) {
        ESP_LOGE(TAG, "esp_transport_poll_read() failed.");
        return ESP_FAIL;
    } else if (poll_read == 0) {
        return ESP_OK;
    }

    // handle header
    memset(&client->ws_data.ws_header, 0, sizeof(ws_header_t));
    while (received_header_len < real_head_len) {
        ESP_LOGI(TAG, "received_header_len(%d), real_head_len(%d)", received_header_len, real_head_len);
        if ((read_tries++) > 5) {
            ESP_LOGE(TAG, "ws_process_receive not finished in 5 transport_read()");
            return ESP_FAIL;
        }
        rlen = esp_transport_read(client->connection_info.transport, (char *)&client->ws_data.ws_header+received_header_len, real_head_len - received_header_len, client->connection_info.network_timeout_ms);
        ESP_LOGI(TAG, "rlen(%d)", rlen);
        if (rlen < 0) {
            ESP_LOGE(TAG, "Read error or end of stream");
            return ESP_FAIL;
        }
        if (rlen == 0 && received_header_len == 0) return ESP_OK;   // no header, continue main loop.
        if (rlen == 0) continue; // got header, continue receive data.

        received_header_len += rlen;

        // calc real_head_len from minimal header
        if (received_header_len == 2) {
            if (client->ws_data.ws_header.payload_len < 126) {
                continue;  // header complete
            } else if (client->ws_data.ws_header.payload_len == 126) {
                real_head_len += 2;
            } else if (client->ws_data.ws_header.payload_len == 127) {
                real_head_len += 8;
            }
        }
    }
    print_bytes(&client->ws_data.ws_header, sizeof(ws_header_t));

    // handle payload_len
    int payload_len = 0;
    int received_payload_len = 0;
    if (client->ws_data.ws_header.payload_len < 126) {
        payload_len = client->ws_data.ws_header.payload_len;
    } else if (client->ws_data.ws_header.payload_len == 126) {
        payload_len = ntohs(*(uint16_t *)client->ws_data.ws_header.ext_payload_len);
    } else if (client->ws_data.ws_header.payload_len == 127) {
        ESP_LOGE(TAG, "payload_len > 0xFFFF");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "payload_len(%d)", payload_len);

    // handle payload
    if (payload_len > client->ws_data.rcv_buff_len) {
        ESP_LOGE(TAG, "payload_len(%d) > rcv_buff_len(%d)", payload_len, client->ws_data.rcv_buff_len);
        // return ESP_FAIL, re-connect to server.
        return ESP_FAIL;
    }
    memset(client->ws_data.rcv_buff, 0, client->ws_data.rcv_buff_len);
    while (received_payload_len < payload_len) {
        ESP_LOGI(TAG, "received_payload_len(%d), payload_len(%d)", received_payload_len, payload_len);
        if ((read_tries++) > 5) {
            ESP_LOGE(TAG, "ws_process_receive not finished in 5 transport_read()");
            return ESP_FAIL;
        }
        rlen = esp_transport_read(client->connection_info.transport, (char *)client->ws_data.rcv_buff+received_payload_len, client->ws_data.rcv_buff_len-received_payload_len, client->connection_info.network_timeout_ms);
        ESP_LOGI(TAG, "rlen(%d)", rlen);
        if (rlen < 0) {
            ESP_LOGE(TAG, "Read error or end of stream");
            return ESP_FAIL;
        }
        if (rlen == 0) { continue; }
        received_payload_len += rlen;
    }

    // handle opcode
    ws_header_t pong_ws_header;
    int n_copy = 0;
    ESP_LOGI(TAG, "opcode(%d)", client->ws_data.ws_header.opcode);
    switch(client->ws_data.ws_header.opcode) {
        case 0: /* FRAGMENT */
        case 1: /* TEXT */
        case 2: /* BINARY */
            client->event.event_id = WS_EVENT_DATA;
            client->event.data = client->ws_data.rcv_buff;
            client->event.data_len = payload_len;
            client->event.ws_header = &(client->ws_data.ws_header);
            ws_dispatch_event(client);

            // handle fragment
            if (client->ws_data.out_buff) {
                ESP_LOGI(TAG, "out_buff_len(%d), out_buff_cur(%d)", client->ws_data.out_buff_len, client->ws_data.out_buff_cur);
                n_copy = (client->ws_data.out_buff_len - client->ws_data.out_buff_cur) > payload_len ? payload_len : (client->ws_data.out_buff_len - client->ws_data.out_buff_cur);
                memcpy(client->ws_data.out_buff + client->ws_data.out_buff_cur, client->ws_data.rcv_buff, n_copy);
                client->ws_data.out_buff_cur += n_copy;

                if (client->ws_data.ws_header.fin) {
                    client->event.event_id = WS_EVENT_DATA_FIN;
                    client->event.data = client->ws_data.out_buff;
                    client->event.data_len = client->ws_data.out_buff_cur;
                    client->event.ws_header = &(client->ws_data.ws_header);
                    ws_dispatch_event(client);
                    client->ws_data.out_buff_cur = 0;
                    memset(client->ws_data.out_buff, 0, client->ws_data.out_buff_len);
                }
            }

            break;
        case 9: /* PING */
            memcpy(&pong_ws_header, &(client->ws_data.ws_header), 2);
            pong_ws_header.opcode = 10;
            pong_ws_header.mask = 1;
            esp_transport_write(client->connection_info.transport, (char *)&pong_ws_header, 2, client->connection_info.network_timeout_ms);

            if (pong_ws_header.payload_len > 0) {
                char mask[4] = {0};
                getrandom(mask, 4, 0);
                esp_transport_write(client->connection_info.transport, mask, 4, client->connection_info.network_timeout_ms);
                for (int i = 0; i < pong_ws_header.payload_len; i++) {
                    client->ws_data.rcv_buff[i] = (client->ws_data.rcv_buff[i] ^ mask[i % 4]);
                }
                esp_transport_write(client->connection_info.transport, (char *)client->ws_data.rcv_buff, pong_ws_header.payload_len, client->connection_info.network_timeout_ms);
            }
            break;
        case 8: /* CLOSE */
        case 10: /* PONG */
        default:
            break;
    }

    return ESP_OK;
}

static int ws_connect(ws_client_handle_t client) {
    if (esp_transport_connect(client->connection_info.transport, client->connection_info.host, client->connection_info.port, client->connection_info.network_timeout_ms) < 0) {
        ESP_LOGE(TAG, "Error connect to ther server esp_transport_connect");
        return -1;
    }

    unsigned char random_key[16];
    getrandom(random_key, sizeof(random_key), 0);

    // Size of base64 coded string is equal '((input_size * 4) / 3) + (input_size / 96) + 6' including Z-term
    unsigned char client_key[28] = {0};

    size_t outlen = 0;
    mbedtls_base64_encode(client_key, sizeof(client_key), &outlen, random_key, sizeof(random_key));
    int len = snprintf((char *)client->ws_data.rcv_buff, client->ws_data.rcv_buff_len,
                         "GET %s HTTP/1.1\r\n"
                         "Connection: Upgrade\r\n"
                         "Host: %s:%d\r\n"
                         "Upgrade: websocket\r\n"
                         "Sec-WebSocket-Version: 13\r\n"
                         "Sec-WebSocket-Protocol: mqtt\r\n"
                         "Sec-WebSocket-Key: %s\r\n"
                         "User-Agent: ESP32 Websocket Client\r\n\r\n",
                         client->connection_info.path,
                         client->connection_info.host,
                         client->connection_info.port,
                         client_key);
    if (len <= 0 || len >= client->ws_data.rcv_buff_len) {
        ESP_LOGE(TAG, "Error in request generation, %d", len);
        return -1;
    }
    ESP_LOGD(TAG, "Write upgrate request\r\n%s", client->ws_data.rcv_buff);
    if (esp_transport_write(client->connection_info.transport, (char *)client->ws_data.rcv_buff, len, client->connection_info.network_timeout_ms) <= 0) {
        ESP_LOGE(TAG, "Error write Upgrade header %s", client->ws_data.rcv_buff);
        return -1;
    }
    if ((len = esp_transport_read(client->connection_info.transport, (char *)client->ws_data.rcv_buff, client->ws_data.rcv_buff_len, client->connection_info.network_timeout_ms)) <= 0) {
        ESP_LOGE(TAG, "Error read response for Upgrade header %s", client->ws_data.rcv_buff);
        return -1;
    }
    char *server_key = get_http_header((char *)client->ws_data.rcv_buff, "Sec-WebSocket-Accept:");
    if (server_key == NULL) {
        ESP_LOGE(TAG, "Sec-WebSocket-Accept not found");
        return -1;
    }

    // See mbedtls_sha1_ret() arg size
    unsigned char expected_server_sha1[20];
    // Size of base64 coded string see above
    unsigned char expected_server_key[33] = {0};
    // If you are interested, see https://tools.ietf.org/html/rfc6455
    const char expected_server_magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char expected_server_text[sizeof(client_key) + sizeof(expected_server_magic) + 1];
    strcpy((char*)expected_server_text, (char*)client_key);
    strcat((char*)expected_server_text, expected_server_magic);

    size_t key_len = strlen((char*)expected_server_text);
    mbedtls_sha1_ret(expected_server_text, key_len, expected_server_sha1);
    mbedtls_base64_encode(expected_server_key, sizeof(expected_server_key),  &outlen, expected_server_sha1, sizeof(expected_server_sha1));
    expected_server_key[ (outlen < sizeof(expected_server_key)) ? outlen : (sizeof(expected_server_key) - 1) ] = 0;
    ESP_LOGD(TAG, "server key=%s, send_key=%s, expected_server_key=%s", (char *)server_key, (char*)client_key, expected_server_key);
    if (strcmp((char*)expected_server_key, (char*)server_key) != 0) {
        ESP_LOGE(TAG, "Invalid websocket key");
        return -1;
    }
    return 0;
}

esp_err_t ws_client_write_data(ws_client_handle_t client, char *buff, int len)
{
    int poll_write;
    if ((poll_write = esp_transport_poll_write(client->connection_info.transport, client->connection_info.network_timeout_ms)) <= 0) {
        ESP_LOGE(TAG, "socket not ready for writing");
        return ESP_FAIL;
    }

    ws_header_t ws_header;
    int header_len = 2;
    ws_header.fin = 1;
    ws_header.opcode = 8; // binary
    ws_header.mask = 1;

    if (len < 126) {
        ws_header.payload_len = len;
    } else if (len >= 126 && len < 0xFFFF) {
        ws_header.payload_len = 126;
        *(uint16_t *)&ws_header.ext_payload_len = htons(len);
        header_len += 2;
    } else {
        ws_header.payload_len = 127;
        /* TOO BIG, we don't support */
        ESP_LOGE(TAG, "payload too big.");
        return ESP_FAIL;
    }

    char mask[4] = {0};
    getrandom(mask, 4, 0);
    for (int i = 0; i < len; i++) {
        buff[i] = (buff[i] ^ mask[i % 4]);
    }

    if (esp_transport_write(client->connection_info.transport, (char *)&ws_header, header_len, client->connection_info.network_timeout_ms) != header_len) {
        ESP_LOGE(TAG, "Error write header");
        return ESP_FAIL;
    }

    if (esp_transport_write(client->connection_info.transport, mask, 4, client->connection_info.network_timeout_ms) != 4) {
        ESP_LOGE(TAG, "Error write mask");
        return ESP_FAIL;
    }

    int write_len = 0;
    int write_tries = 0;
    int rlen = 0;
    while (write_len < len) {
        ESP_LOGI(TAG, "write_len(%d), len(%d)", write_len, len);
        if ((write_tries++) > 5) {
            ESP_LOGE(TAG, "ws_client_write_data not finished in 5 esp_transport_write()");
            return ESP_FAIL;
        }
        rlen = esp_transport_write(client->connection_info.transport, buff, len, client->connection_info.network_timeout_ms);
        if (rlen < 0) {
            ESP_LOGE(TAG, "Error write data or timeout, rlen = %d", rlen);
            return ESP_FAIL;
        }
        write_len += rlen;
    }

    return ESP_OK;
}

esp_err_t ws_client_destroy(ws_client_handle_t client) {
    ws_client_stop(client);
    free(client->connection_info.host);
    free(client->connection_info.path);
    free(client->connection_info.scheme);
    esp_transport_list_destroy(client->connection_info.transport_list);
    vEventGroupDelete(client->status_bits);
    free(client->ws_data.rcv_buff);
    free(client);
    return ESP_OK;
}

esp_err_t ws_client_stop(ws_client_handle_t client) {
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

static esp_err_t ws_abort_connection(ws_client_handle_t client)
{
    esp_transport_close(client->connection_info.transport);
    client->state = WS_STATE_WAIT_TIMEOUT;
    ESP_LOGI(TAG, "Reconnect after %d ms", client->connection_info.network_timeout_ms);
    client->event.event_id = WS_EVENT_DISCONNECTED;
    ws_dispatch_event(client);
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

static char *trimwhitespace(const char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) {
        return (char *)str;
    }

    // Trim trailing space
    end = (char *)(str + strlen(str) - 1);
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = 0;

    return (char *)str;
}

static char *get_http_header(const char *buffer, const char *key)
{
    char *found = strstr(buffer, key);
    if (found) {
        found += strlen(key);
        char *found_end = strstr(found, "\r\n");
        if (found_end) {
            found_end[0] = 0;//terminal string

            return trimwhitespace(found);
        }
    }
    return NULL;
}