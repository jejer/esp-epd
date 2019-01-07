#ifndef _EPDIF_H_
#define _EPDIF_H_

#include <stdint.h>

typedef struct epdif_pin_config {
    int mosi_io_num;
    int sclk_io_num;
    int dc_io_num;
    int cs_io_num;
    int busy_io_num;
    int rst_io_num;
    int vcc_io_num;
} epdif_pin_config_t;

void epdif_init(epdif_pin_config_t* pin_cfg, uint32_t width, uint32_t height);
void epdif_reset();
void epdif_delay_ms(uint32_t delaytime);
void epdif_wait_until_idle();
void epdif_send_command(uint8_t cmd);
void epdif_send_byte_data(uint8_t data);
void epdif_send_data(const uint8_t* data, uint32_t len);

#endif
