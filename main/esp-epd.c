#include "epdif.h"
#include "epd2in9.h"
#include "esp-ui.h"
#include "ws_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/apps/sntp.h"
#include "cJSON.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ESP-EPD";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static int s_wifi_retry_num = 0;
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event);
static void wifi_init_sta();

static void initialize_sntp(void);
static void refresh_time_task(void *pvParameter);

static esp_err_t spiffs_init();

static esp_err_t ws_handler(ws_event_t *event);
static void pushbullet_mirror_msg(char *json);

void app_main() {
    esp_err_t ret;

    // init NVS and WIFI
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // init NTP
    initialize_sntp();

    // init epd spi interface
    epdif_pin_config_t epd_pin_cfg = {
        .mosi_io_num = 14,
        .sclk_io_num = 13,
        .dc_io_num = 27,
        .cs_io_num = 15,
        .busy_io_num = 25,
        .rst_io_num = 26,
        .vcc_io_num = 0,
    };
    epdif_init(&epd_pin_cfg, EPD_WIDTH, EPD_HEIGHT);

    // init spiffs
    spiffs_init();

    // init esp-ui
    esp_ui_init();

    // start tasks
    xTaskCreate(&refresh_time_task, "refresh_time_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ws_client_config_t ws_cfg = {
        .uri = "wss://stream.pushbullet.com/websocket/"CONFIG_PUSHBULLET_TOKEN,
        .event_handle = ws_handler,
        .out_buffer = calloc(1, 5*1024),
        .out_buffer_size = 5*1024,
    };
    ws_client_handle_t ws_client = ws_client_init(&ws_cfg);
    ret = ws_client_start(ws_client);
    ESP_ERROR_CHECK(ret);
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_wifi_retry_num++;
            ESP_LOGI(TAG,"retry to connect to the AP, retry_counter=%d", s_wifi_retry_num);
            break;
        }
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "cn.pool.ntp.org");
    sntp_init();
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1); tzset();
}

void refresh_time_task(void *pvParameter) {
    time_t now = 0;
    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, 64, "%c", &timeinfo);
        //ESP_LOGI(TAG, "NTP: The current date/time in Shanghai is: %s", strftime_buf);
        esp_ui_update();
        vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
    }
}

static esp_err_t spiffs_init() {
    esp_err_t ret;
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = false,
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }
    return ret;
}

static esp_err_t ws_handler(ws_event_t *event) {
    switch (event->event_id) {
        case WS_EVENT_ERROR:
            ESP_LOGE(TAG, "WS_EVENT_ERROR");
            break;
        case WS_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WS_EVENT_CONNECTED");
            break;
        case WS_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WS_EVENT_DISCONNECTED");
            break;
        case WS_EVENT_DATA_FIN:
            ESP_LOGI(TAG, "WS_EVENT_DATA_FIN");
            //print_bytes(event->data, event->data_len);
            ESP_LOGI(TAG, "TEXT LEN: %d", event->data_len);
            ESP_LOGI(TAG, "TEXT: %.*s", event->data_len, event->data);
            pushbullet_mirror_msg((char *)event->data);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void pushbullet_mirror_msg(char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *root_type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!root_type || strncmp(root_type->valuestring, "push", 4) != 0) {cJSON_Delete(root); return;}

    cJSON *push = cJSON_GetObjectItemCaseSensitive(root, "push");
    if (!push) {cJSON_Delete(root); return;}

    cJSON *push_type = cJSON_GetObjectItemCaseSensitive(push, "type");
    if (push_type && strncmp(push_type->valuestring ,"dismissal", 9) == 0) {
        ui_data.message[0] = 0;
        esp_ui_update();
        cJSON_Delete(root); return;
    }
    if (!push_type || strncmp(push_type->valuestring ,"mirror", 6) != 0) {cJSON_Delete(root); return;}

    cJSON *body = cJSON_GetObjectItemCaseSensitive(push, "body");
    if (!body || body->type != cJSON_String) {cJSON_Delete(root); return;}

    //printf(body->valuestring);
    strncpy(ui_data.message, body->valuestring, 128*3);
    esp_ui_update();
    cJSON_Delete(root); return;
}