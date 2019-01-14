#include "esp-ui.h"
#include "epdpaint.h"
#include "epd2in9.h"
#include "epdfont.h"

#include "esp_log.h"
#include "esp_spiffs.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#define ROTATE ROTATE_270
static const char *TAG = "ESP-UI";

ui_data_t ui_data;
static epd_font_t hzk;

int esp_ui_init() {
    // init hzk chinese gb2312 font
    FILE* f = fopen("/spiffs/HZK12", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file /spiffs/HZK12");
        return false;
    }
    hzk.width = 16;
    hzk.height = 12;
    hzk.file = f;

    return true;
}

void esp_ui_full_paint() {
    ui_data.refresh_counter++;
    // init display
    epd_init(EPD_2IN9_LUT_UPDATE_FULL);

    // create painter
    esp_painter_handle_t painter = epdpaint_init(ROTATE, 0, 0, EPD_HEIGHT, EPD_WIDTH);
    if (!painter) return;

    // start paint
    epdpaint_clear(painter, WHITE);

    // paint time
    time_t now = 0;
    time(&now);
    struct tm timeinfo = { 0 };
    localtime_r(&now, &timeinfo);
    char strftime_buf[6];
    strftime(strftime_buf, 6, "%H:%M", &timeinfo);
    epdpaint_draw_utf8_string(painter,
                              EPD_HEIGHT-5*epd_font_asc_16.width,
                              EPD_WIDTH-epd_font_asc_16.height,
                              5*epd_font_asc_16.width,
                              epd_font_asc_16.height,
                              strftime_buf,
                              &epd_font_asc_16, 0, BLACK);

    // paint pushbullet message
    epdpaint_draw_utf8_string(painter, 0, 0, 296, 128, ui_data.message, &epd_font_asc_16, &hzk, BLACK);

    epd_set_frame_memory(painter->buffer);
    epd_display_frame();
    epd_set_frame_memory(painter->buffer);
    epd_sleep();

    epdpaint_destroy(painter);

    return;
}

// partial display format: 23:23
void esp_ui_paint_time() {
    // check refresh mode
    ESP_LOGI(TAG, "refresh_counter(%d)", ui_data.refresh_counter);
    if (ui_data.refresh_counter % 10 == 0) {
        return esp_ui_full_paint();
    }
    ui_data.refresh_counter++;
    epd_init(EPD_2IN9_LUT_UPDATE_PART);

    epd_font_t *time_fnt = &epd_font_asc_16;

    // create painter
    esp_painter_handle_t painter = epdpaint_init(ROTATE, EPD_HEIGHT-5*time_fnt->width, EPD_WIDTH-time_fnt->height, (time_fnt->width*5), time_fnt->height);
    if (!painter) {
        ESP_LOGE(TAG, "no memory for painter");
        return;
    }

    // start paint
    epdpaint_clear(painter, WHITE);

    // paint time
    time_t now = 0;
    time(&now);
    struct tm timeinfo = { 0 };
    localtime_r(&now, &timeinfo);
    char strftime_buf[6];
    strftime(strftime_buf, 6, "%H:%M", &timeinfo);
    epdpaint_draw_utf8_string(painter, 0, 0,
                              5*time_fnt->width,
                              time_fnt->height,
                              strftime_buf,
                              time_fnt, 0, BLACK);

    epd_set_image_memory(painter->buffer, painter->abs_x, painter->abs_y, painter->abs_width, painter->abs_height);
    epd_display_frame();
    epd_sleep();

    epdpaint_destroy(painter);
}