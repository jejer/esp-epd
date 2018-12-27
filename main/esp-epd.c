#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/apps/sntp.h"

#include "epdif.h"
#include "epd2in9.h"
#include "epdpaint.h"
#include "epdfont.h"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "ESP-EPD";

static int s_retry_num = 0;

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
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void obtain_time(void)
{
    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

void app_main() {
    epd_pin_cfg.mosi_io_num = 14;
    epd_pin_cfg.sclk_io_num = 13;
    epd_pin_cfg.dc_io_num = 27;
    epd_pin_cfg.cs_io_num = 15;
    epd_pin_cfg.busy_io_num = 25;
    epd_pin_cfg.rst_io_num = 26;
    epd_pin_cfg.vcc_io_num = 0;

    uint8_t *frame_buff = heap_caps_malloc((EPD_WIDTH/8) * EPD_HEIGHT, MALLOC_CAP_DMA);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // NTP
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    initialize_sntp();
    obtain_time();
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1); tzset();
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

    // fill black
    memset(frame_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_full_update, 1);
    epd_set_frame_memory_full_screen(frame_buff);
    epd_display_frame();

    // fill white
    memset(frame_buff, 0xFF, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_set_frame_memory_full_screen(frame_buff);
    epd_display_frame();
    epd_set_frame_memory_full_screen(frame_buff);

    // draw a black rectangle
    memset(frame_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_set_frame_memory(frame_buff, 0, 0, 50, 100);
    epd_display_frame();

    // fill black
    memset(frame_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_set_frame_memory_full_screen(frame_buff);
    epd_display_frame();
    epd_set_frame_memory_full_screen(frame_buff);

    // partial draw white rectangle
    memset(frame_buff, 0xFF, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_partial_update, 0);
    epd_set_frame_memory(frame_buff, 0, 0, 50, 100);
    epd_display_frame();

    // init spiffs
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = false
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("SPIFFS", "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE("SPIFFS", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    // init hzk16 chinese gb2312 font
    FILE* f = fopen("/spiffs/hzk16.bin", "r");
    if (f == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file /spiffs/hzk16.bin");
        return;
    }
    epd_font hzk16 = {
        .width = 16,
        .height = 16,
        .file = f,
    };

    // epdpaint and font test
    epdpaint_frame_buffer = frame_buff;
    epdpaint_clear(0);
    epdpaint_draw_utf8_string(0, 0, "aA测试Bb!", &epd_font_ascii_16, &hzk16, 1);
    epdpaint_draw_circle(150, 60, 40, 1);
    epdpaint_draw_filled_circle(150, 60, 20, 1);
    epdpaint_draw_rectangle(32, 0, 64, 20, 1);
    epdpaint_draw_filled_rectangle(32, 80, 64, 100, 1);
    epd_set_frame_memory_full_screen(frame_buff);
    epd_display_frame();
    epd_sleep();
}