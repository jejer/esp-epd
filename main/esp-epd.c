#include "epd2in9.h"
#include "epdif.h"
#include "epdpaint.h"
#include "epdfont.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include <string.h>


void app_main() {
    epd_pin_cfg.mosi_io_num = 14;
    epd_pin_cfg.sclk_io_num = 13;
    epd_pin_cfg.dc_io_num = 27;
    epd_pin_cfg.cs_io_num = 15;
    epd_pin_cfg.busy_io_num = 25;
    epd_pin_cfg.rst_io_num = 26;
    epd_pin_cfg.vcc_io_num = 0;

    uint8_t *frame_buff = heap_caps_malloc((EPD_WIDTH/8) * EPD_HEIGHT, MALLOC_CAP_DMA);

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
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
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