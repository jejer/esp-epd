#include "epd2in9.h"
#include "epdif.h"
#include "esp_system.h"
#include "driver/spi_master.h"

void epd_set_lut(const unsigned char* lut) {
    epdif_send_command(WRITE_LUT_REGISTER);
    /* the length of look-up table is 30 bytes */
    for (int i = 0; i < 30; i++) {
        epdif_send_byte_data(lut[i]);
    }
}

void epd_reset() {
    gpio_set_level(epd_pin_cfg.rst_io_num, 0);
    epdif_delayms(200);
    gpio_set_level(epd_pin_cfg.rst_io_num, 1);
    epdif_delayms(200);
}

int epd_init(const unsigned char* lut, int init_epdif) {
    if (init_epdif && epdif_init(EPD_WIDTH, EPD_HEIGHT) != 0) {
        return -1;
    }
    /* EPD hardware init start */
    epd_reset();
    epdif_send_command(DRIVER_OUTPUT_CONTROL);
    epdif_send_byte_data((EPD_HEIGHT - 1) & 0xFF);
    epdif_send_byte_data(((EPD_HEIGHT - 1) >> 8) & 0xFF);
    epdif_send_byte_data(0x00);                     // GD = 0; SM = 0; TB = 0;
    epdif_send_command(BOOSTER_SOFT_START_CONTROL);
    epdif_send_byte_data(0xD7);
    epdif_send_byte_data(0xD6);
    epdif_send_byte_data(0x9D);
    epdif_send_command(WRITE_VCOM_REGISTER);
    epdif_send_byte_data(0xA8);                     // VCOM 7C
    epdif_send_command(SET_DUMMY_LINE_PERIOD);
    epdif_send_byte_data(0x1A);                     // 4 dummy lines per gate
    epdif_send_command(SET_GATE_TIME);
    epdif_send_byte_data(0x08);                     // 2us per line
    epdif_send_command(DATA_ENTRY_MODE_SETTING);
    epdif_send_byte_data(0x03);                     // X increment; Y increment
    epd_set_lut(lut);
    /* EPD hardware init end */
    return 0;
}

void epd_sleep() {
    epdif_send_command(DEEP_SLEEP_MODE);
    epdif_wait_until_idle();
}

void epd_set_memory_pointer(int x, int y) {
    epdif_send_command(SET_RAM_X_ADDRESS_COUNTER);
    /* x point must be the multiple of 8 or the last 3 bits will be ignored */
    epdif_send_byte_data((x >> 3) & 0xFF);
    epdif_send_command(SET_RAM_Y_ADDRESS_COUNTER);
    epdif_send_byte_data(y & 0xFF);
    epdif_send_byte_data((y >> 8) & 0xFF);
    epdif_wait_until_idle();
}

void epd_set_memory_area(int x_start, int y_start, int x_end, int y_end) {
    epdif_send_command(SET_RAM_X_ADDRESS_START_END_POSITION);
    /* x point must be the multiple of 8 or the last 3 bits will be ignored */
    epdif_send_byte_data((x_start >> 3) & 0xFF);
    epdif_send_byte_data((x_end >> 3) & 0xFF);
    epdif_send_command(SET_RAM_Y_ADDRESS_START_END_POSITION);
    epdif_send_byte_data(y_start & 0xFF);
    epdif_send_byte_data((y_start >> 8) & 0xFF);
    epdif_send_byte_data(y_end & 0xFF);
    epdif_send_byte_data((y_end >> 8) & 0xFF);
}

void epd_set_frame_memory(const unsigned char* image_buffer, int x, int y, int image_width, int image_height) {
    int x_end;
    int y_end;

    /* x point must be the multiple of 8 or the last 3 bits will be ignored */
    x &= 0xF8;
    image_width &= 0xF8;

    if (image_buffer == NULL || x < 0 || image_width < 1 || y < 0 || image_height < 1) {
        return;
    }

    if (x + image_width >= EPD_WIDTH) {
        x_end = EPD_WIDTH - 1;
    } else {
        x_end = x + image_width - 1;
    }

    if (y + image_height >= EPD_HEIGHT) {
        y_end = EPD_HEIGHT - 1;
    } else {
        y_end = y + image_height - 1;
    }

    epd_set_memory_area(x, y, x_end, y_end);
    epd_set_memory_pointer(x, y);
    epdif_send_command(WRITE_RAM);
    /* send the image data */
    for (int j = 0; j < y_end - y + 1; j++) {
        for (int i = 0; i < (x_end - x + 1) / 8; i++) {
            epdif_send_byte_data(image_buffer[i + j * (image_width / 8)]);
        }
    }
}

void epd_set_frame_memory_full_screen(const unsigned char* image_buffer) {
    if (image_buffer == NULL) {
        return;
    }

    epd_set_memory_area(0, 0, EPD_WIDTH - 1, EPD_HEIGHT - 1);
    epd_set_memory_pointer(0, 0);
    epdif_send_command(WRITE_RAM);
    epdif_send_data(image_buffer, (EPD_WIDTH/8) * EPD_HEIGHT);
}

void epd_display_frame() {
    epdif_send_command(DISPLAY_UPDATE_CONTROL_2);
    epdif_send_byte_data(0xC4);
    epdif_send_command(MASTER_ACTIVATION);
    epdif_send_command(TERMINATE_FRAME_READ_WRITE);
    epdif_wait_until_idle();
}

const unsigned char lut_full_update[] =
{
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22, 
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88, 
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51, 
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00
};

const unsigned char lut_partial_update[] =
{
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x44, 0x12, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};