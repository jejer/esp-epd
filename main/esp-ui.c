#include "esp-ui.h"
#include "epdpaint.h"
#include "epd2in9.h"
#include "epdfont.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ESP-UI";

ui_data_t ui_data;
static epd_font_t *phzk = 0;
static portMUX_TYPE paint_lock = portMUX_INITIALIZER_UNLOCKED;

int esp_ui_init() {
    // init hzk chinese gb2312 font
    FILE* f = fopen("/spiffs/HZK12", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file /spiffs/HZK12");
        return false;
    }
    epd_font_t hzk = {
        .width = 16,
        .height = 12,
        .file = f,
    };
    phzk = &hzk;
    return true;
}

void esp_ui_update() {
    // allocate EPD frame buffer
    uint8_t *frame_buff = heap_caps_malloc((EPD_WIDTH/8) * EPD_HEIGHT, MALLOC_CAP_DMA);
    if (!frame_buff) {
        ESP_LOGE(TAG, "no memory for frame buffer");
        return;
    }

    portENTER_CRITICAL(&paint_lock);

    // init display
    ESP_LOGI(TAG, "partial_display_cnt(%d)", ui_data.partial_display_cnt);
    if (ui_data.partial_display_cnt++ % 10 == 0) {
        ui_data.partial_display_cnt = 0;
        epd_init(EPD_2IN9_LUT_UPDATE_FULL);
    } else epd_init(EPD_2IN9_LUT_UPDATE_PART);

    // init paint
    epdpaint_init(frame_buff, ROTATE_270);

    // start paint
    epdpaint_clear(WHITE);

    // paint time
    time_t now = 0;
    time(&now);
    struct tm timeinfo = { 0 };
    localtime_r(&now, &timeinfo);
    char strftime_buf[6];
    strftime(strftime_buf, 6, "%H:%M", &timeinfo);
    epdpaint_draw_utf8_string(EPD_HEIGHT-5*epd_font_asc_16.width, EPD_WIDTH-epd_font_asc_16.height, 5*epd_font_asc_16.width, epd_font_asc_16.height, strftime_buf, &epd_font_asc_16, 0, BLACK);

    // paint pushbullet message
    epdpaint_draw_utf8_string(0, 0, 296, 128, ui_data.message, &epd_font_asc_16, phzk, BLACK);

    epdpaint_draw_utf8_string(0, 0, 296, 128, "本条目没有列出任何参考或来源。（2013年11月10日） 维基百科所有的内容都应该可供查证。请协助添加来自可靠来源的引用以改善这篇条目。无法查证的内容可能被提出异议而移除。在计算机科学与数学中，一个排序算法（英语：Sorting algorithm）是一种能将一串数据依照特定排序方式进行排列的一种算法。最常用到的排序方式是数值顺序以及字典顺序。", &epd_font_asc_16, phzk, BLACK);


    epd_set_frame_memory(frame_buff);
    epd_display_frame();
    epd_sleep();

    free(frame_buff);

    portEXIT_CRITICAL(&paint_lock);
    return;
}