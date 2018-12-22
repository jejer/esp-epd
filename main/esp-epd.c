#include "epd2in9.h"
#include "epdif.h"

#include "esp_heap_caps.h"
#include <string.h>


void app_main() {
    epd_pin_cfg.mosi_io_num = 14;
    epd_pin_cfg.sclk_io_num = 13;
    epd_pin_cfg.dc_io_num = 27;
    epd_pin_cfg.cs_io_num = 15;
    epd_pin_cfg.busy_io_num = 25;
    epd_pin_cfg.rst_io_num = 26;
    epd_pin_cfg.vcc_io_num = 0;

    uint8_t *dis_buff = heap_caps_malloc((EPD_WIDTH/8) * EPD_HEIGHT, MALLOC_CAP_DMA);

    // fill black
    memset(dis_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_full_update, 1);
    epd_set_frame_memory_full_screen(dis_buff);
    epd_display_frame();
    epd_sleep();

    // fill white
    memset(dis_buff, 0xFF, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_full_update, 0);
    epd_set_frame_memory_full_screen(dis_buff);
    epd_display_frame();
    epd_set_frame_memory_full_screen(dis_buff);
    epd_sleep();

    // draw a black rectangle
    memset(dis_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_full_update, 0);
    epd_set_frame_memory(dis_buff, 0, 0, 50, 100);
    epd_display_frame();
    epd_sleep();

    // fill black
    memset(dis_buff, 0x00, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_full_update, 0);
    epd_set_frame_memory_full_screen(dis_buff);
    epd_display_frame();
    epd_set_frame_memory_full_screen(dis_buff);
    epd_sleep();

    // partial draw white rectangle
    memset(dis_buff, 0xFF, (EPD_WIDTH/8) * EPD_HEIGHT);
    epd_init(lut_partial_update, 0);
    epd_set_frame_memory(dis_buff, 0, 0, 50, 100);
    epd_display_frame();
    epd_sleep();
}