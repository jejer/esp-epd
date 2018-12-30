#include "epd2in9.h"
#include "epdif.h"

// EPD2IN9 commands
#define DRIVER_OUTPUT_CONTROL                       0x01
#define BOOSTER_SOFT_START_CONTROL                  0x0C
#define GATE_SCAN_START_POSITION                    0x0F
#define DEEP_SLEEP_MODE                             0x10
#define DATA_ENTRY_MODE_SETTING                     0x11
#define SW_RESET                                    0x12
#define TEMPERATURE_SENSOR_CONTROL                  0x1A
#define MASTER_ACTIVATION                           0x20
#define DISPLAY_UPDATE_CONTROL_1                    0x21
#define DISPLAY_UPDATE_CONTROL_2                    0x22
#define WRITE_RAM                                   0x24
#define WRITE_VCOM_REGISTER                         0x2C
#define WRITE_LUT_REGISTER                          0x32
#define SET_DUMMY_LINE_PERIOD                       0x3A
#define SET_GATE_TIME                               0x3B
#define BORDER_WAVEFORM_CONTROL                     0x3C
#define SET_RAM_X_ADDRESS_START_END_POSITION        0x44
#define SET_RAM_Y_ADDRESS_START_END_POSITION        0x45
#define SET_RAM_X_ADDRESS_COUNTER                   0x4E
#define SET_RAM_Y_ADDRESS_COUNTER                   0x4F
#define TERMINATE_FRAME_READ_WRITE                  0xFF

static const uint8_t lut_full_update[] =
{
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22, 
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88, 
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51, 
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00
};

static const uint8_t lut_partial_update[] =
{
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x44, 0x12, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void epd_set_lut(const uint8_t* lut) {
    epdif_send_command(WRITE_LUT_REGISTER);
    /* the length of look-up table is 30 bytes */
    epdif_send_data(lut, 30);
}

void epd_init(int lut_update_mode) {
    /* EPD hardware init start */
    epdif_reset();
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
    if (lut_update_mode == EPD_2IN9_LUT_UPDATE_FULL) {
        epd_set_lut(lut_full_update);
    } else if (lut_update_mode == EPD_2IN9_LUT_UPDATE_PART) {
        epd_set_lut(lut_partial_update);
    }
    /* EPD hardware init end */
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

void epd_set_image_memory(const uint8_t* image_buffer, int x, int y, int width, int height) {
    int x_end;
    int y_end;

    /* x point must be the multiple of 8 or the last 3 bits will be ignored */
    x &= 0xF8;
    width &= 0xF8;

    if (image_buffer == NULL || x < 0 || width < 1 || y < 0 || height < 1) {
        return;
    }

    if (x + width >= EPD_WIDTH) {
        x_end = EPD_WIDTH - 1;
    } else {
        x_end = x + width - 1;
    }

    if (y + height >= EPD_HEIGHT) {
        y_end = EPD_HEIGHT - 1;
    } else {
        y_end = y + height - 1;
    }

    epd_set_memory_area(x, y, x_end, y_end);
    epd_set_memory_pointer(x, y);
    epdif_send_command(WRITE_RAM);
    /* send the image data */
    for (int j = 0; j < y_end - y + 1; j++) {
        for (int i = 0; i < (x_end - x + 1) / 8; i++) {
            epdif_send_byte_data(image_buffer[i + j * (width / 8)]);
        }
    }
}

void epd_set_frame_memory(const uint8_t* frame_buffer) {
    if (frame_buffer == NULL) {
        return;
    }

    epd_set_memory_area(0, 0, EPD_WIDTH - 1, EPD_HEIGHT - 1);
    epd_set_memory_pointer(0, 0);
    epdif_send_command(WRITE_RAM);
    epdif_send_data(frame_buffer, (EPD_WIDTH/8) * EPD_HEIGHT);
}

void epd_display_frame() {
    epdif_send_command(DISPLAY_UPDATE_CONTROL_2);
    epdif_send_byte_data(0xC4);
    epdif_send_command(MASTER_ACTIVATION);
    epdif_send_command(TERMINATE_FRAME_READ_WRITE);
    epdif_wait_until_idle();
}
