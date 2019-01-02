#include "epdif.h"
#include "epd2in9.h"
#include "epdpaint.h"
#include "epdfont.h"
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

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static int s_retry_num = 0;
static esp_err_t event_handler(void *ctx, system_event_t *event);
void wifi_init_sta();
static void initialize_sntp(void);
void print_time_task(void *pvParameter);

void print_bytes(void *ptr, int size) 
{
    unsigned char *p = ptr;
    int i;
    for (i=0; i<size; i++) {
        printf("%02hhX ", p[i]);
    }
    printf("\n");
}

static bool pushbullet_mirror_msg(char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *root_type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!root_type || strncmp(root_type->valuestring, "push", 4) != 0) {cJSON_Delete(root); return true;}

    cJSON *push = cJSON_GetObjectItemCaseSensitive(root, "push");
    if (!push) {cJSON_Delete(root); return true;}

    cJSON *push_type = cJSON_GetObjectItemCaseSensitive(push, "type");
    if (!push_type || strncmp(push_type->valuestring ,"mirror", 6) != 0) {cJSON_Delete(root); return true;}

    cJSON *body = cJSON_GetObjectItemCaseSensitive(push, "body");
    if (!body || body->type != cJSON_String) {cJSON_Delete(root); return true;}

    printf(body->valuestring);
    cJSON_Delete(root); return true;
}

static esp_err_t ws_handler(ws_event_t *event) {
    static char *text_merged = NULL;
    if (!text_merged) {text_merged = malloc(1024*3); memset(text_merged, 0, 1024*3);}
    if (!text_merged) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

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
        case WS_EVENT_DATA:
            ESP_LOGI(TAG, "WS_EVENT_DATA");
            //print_bytes(event->data, event->data_len);
            ESP_LOGI(TAG, "TEXT LEN: %d", event->data_len);
            ESP_LOGI(TAG, "TEXT: %.*s", event->data_len, event->data);
            if (event->data_len > 1500) {
                ESP_LOGI(TAG, "TEXT LEN TOO BIG");
                memset(text_merged, 0, 1024*3);
                break;
            }
            if (strlen(text_merged) == 0 && event->data[0] == '{') {
                ESP_LOGI(TAG, "TEXT 1st PART");
                strncpy(text_merged, (const char *)event->data, event->data_len);
                ESP_LOGI(TAG, "TEXT 1st PART, strlen(text_merged) = %d", strlen(text_merged));
                ESP_LOGI(TAG, "%s", text_merged);
                if (pushbullet_mirror_msg(text_merged)) {
                    ESP_LOGI(TAG, "TEXT PARSED");
                    memset(text_merged, 0, 1024*3);
                }
            } else if (strlen(text_merged) > 0 && event->data[event->data_len-1] == '}') {
                ESP_LOGI(TAG, "TEXT 2nd PART");
                strncpy(text_merged + strlen(text_merged), (const char *)event->data, event->data_len);
                pushbullet_mirror_msg(text_merged);
                ESP_LOGI(TAG, "TEXT 2nd PART COMPLETE");
                memset(text_merged, 0, 1024*3);
            } else {
                ESP_LOGI(TAG, "OTHER CASE!!");
                ESP_LOGI(TAG, "strlen(text_merged) = %d", strlen(text_merged));
                ESP_LOGI(TAG, "last 10 chars: %.*s", 10, event->data+event->data_len-10);
                ESP_LOGI(TAG, "last char: %c", event->data[event->data_len-1]);
                memset(text_merged, 0, 1024*3);
            }
            memset(event->data, 0, 2*1024);
            break;
        default:
            break;
    }
    return ESP_OK;
}

char * get_commit() {
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    esp_err_t err;
    esp_http_client_config_t config = {
        .url = "https://api.github.com/repos/jejer/esp-epd/commits",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

    // currently esp_http_client_perform() could not handle (content_len + header_len) > (buffer_size or MTU). use stream mode to read.
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return NULL;
    }
    int content_length = esp_http_client_fetch_headers(client);
    char *buffer = malloc(content_length);
    int total_read_len = 0, read_len;
    if (total_read_len < content_length) {
        read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len <= 0) {
            ESP_LOGE(TAG, "Error read data");
        }
        buffer[read_len] = 0;
        ESP_LOGD(TAG, "read_len = %d", read_len);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(buffer); free(buffer); assert(root);
    cJSON *latest = cJSON_GetArrayItem(root, 0); assert(latest);
    cJSON *commit = cJSON_GetObjectItemCaseSensitive(latest, "commit"); assert(commit);
    cJSON *message = cJSON_GetObjectItemCaseSensitive(commit, "message"); assert(message);

    char *ret = malloc(1024);
    sprintf(ret, "消息: %s", message->valuestring);

    cJSON_Delete(root);
    return ret;
}

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

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ws_client_config_t ws_cfg = {
        .uri = "wss://stream.pushbullet.com/websocket/<your token here>",
        .event_handle = ws_handler,
    };
    ws_client_handle_t ws_client = ws_client_init(&ws_cfg);
    ret = ws_client_start(ws_client);
    ESP_ERROR_CHECK(ret);

    // init NTP
    initialize_sntp();
    xTaskCreate(&print_time_task, "print_time_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

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

    // allocate EPD frame buffer
    uint8_t *frame_buff = heap_caps_malloc((EPD_WIDTH/8) * EPD_HEIGHT, MALLOC_CAP_DMA);
    if (!frame_buff) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

    // epdpaint init
    epdpaint_init(frame_buff, ROTATE_270);

    // init spiffs
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
        return;
    }

    // init hzk16 chinese gb2312 font
    FILE* f = fopen("/spiffs/hzk16.bin", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file /spiffs/hzk16.bin");
        return;
    }
    epd_font_t hzk16 = {
        .width = 16,
        .height = 16,
        .file = f,
    };

    // epd init with full update
    epd_init(EPD_2IN9_LUT_UPDATE_FULL);

    // fill black
    epdpaint_clear(BLACK);
    epd_set_frame_memory(frame_buff);
    epd_display_frame();

    // fill white
    epdpaint_clear(WHITE);
    epd_set_frame_memory(frame_buff);
    epd_display_frame();

    // draw a black rectangle on white background
    epdpaint_clear(WHITE);
    epd_set_frame_memory(frame_buff);
    memset(frame_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_set_image_memory(frame_buff, 0, 0, 50, 100);
    epd_display_frame();

    // fill black
    epdpaint_clear(BLACK);
    epd_set_frame_memory(frame_buff);
    epd_display_frame();

    // partial draw white rectangle on black background
    epd_init(EPD_2IN9_LUT_UPDATE_PART);
    epdpaint_clear(BLACK);
    epd_set_frame_memory(frame_buff);
    memset(frame_buff, 0xFF, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_set_image_memory(frame_buff, 0, 0, 50, 100);
    epd_display_frame();

    // epdpaint and font test
    epd_init(EPD_2IN9_LUT_UPDATE_FULL);
    epdpaint_clear(WHITE);
    epdpaint_draw_utf8_string(0, 0, "aA测试Bb!", &epd_font_ascii_16, &hzk16, BLACK);
    epdpaint_draw_circle(150, 60, 40, BLACK);
    epdpaint_draw_filled_circle(150, 60, 20, BLACK);
    epdpaint_draw_rectangle(32, 0, 64, 20, BLACK);
    epdpaint_draw_filled_rectangle(32, 80, 64, 100, BLACK);
    epdpaint_draw_utf8_string(0, 40, get_commit(), &epd_font_ascii_16, &hzk16, BLACK);
    epd_set_frame_memory(frame_buff);
    epd_display_frame();
    epd_set_frame_memory(frame_buff);
    epd_sleep();
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_retry_num++;
            ESP_LOGI(TAG,"retry to connect to the AP, retry_counter=%d", s_retry_num);
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
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

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

void print_time_task(void *pvParameter) {
    time_t now = 0;
    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, 64, "%c", &timeinfo);
        ESP_LOGI(TAG, "NTP: The current date/time in Shanghai is: %s", strftime_buf);
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}