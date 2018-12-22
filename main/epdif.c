#include "epdif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include <string.h>

epd_pin_config_t epd_pin_cfg;
spi_device_handle_t epd_spi;

int epdif_init(uint32_t width, uint32_t height) {
    gpio_set_direction(epd_pin_cfg.rst_io_num, GPIO_MODE_OUTPUT);
    gpio_set_level(epd_pin_cfg.rst_io_num, 0);
    gpio_set_direction(epd_pin_cfg.dc_io_num, GPIO_MODE_OUTPUT);
    gpio_set_level(epd_pin_cfg.dc_io_num, 1);
    gpio_set_direction(epd_pin_cfg.busy_io_num, GPIO_MODE_INPUT);
    gpio_set_pull_mode(epd_pin_cfg.busy_io_num, GPIO_PULLUP_ONLY);

    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num = -1,
        .mosi_io_num = epd_pin_cfg.mosi_io_num,
        .sclk_io_num = epd_pin_cfg.sclk_io_num,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (width/8) * height,
    };

    spi_device_interface_config_t devcfg={
        .clock_speed_hz = 40*1000*1000,           //Clock out at 40 MHz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = epd_pin_cfg.cs_io_num,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };

    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
    ESP_ERROR_CHECK(ret);
    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &epd_spi);
    ESP_ERROR_CHECK(ret);
    printf("SPI: display device added to spi bus\r\n");
    return 0;
}

void epdif_delayms(uint32_t delaytime) {
    vTaskDelay(delaytime / portTICK_PERIOD_MS);
}

void epdif_wait_until_idle() {
    while(gpio_get_level(epd_pin_cfg.busy_io_num) == 1) {      //LOW: idle, HIGH: busy
        epdif_delayms(100);
    }
}

void epdif_send_command(uint8_t cmd) {
    gpio_set_level(epd_pin_cfg.dc_io_num, 0);

    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                 //Len is in bytes, transaction length is in bits.
    t.tx_data[0]=cmd;               //Data
    t.flags=SPI_TRANS_USE_TXDATA;
    ret=spi_device_polling_transmit(epd_spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

void epdif_send_byte_data(uint8_t data) {
    gpio_set_level(epd_pin_cfg.dc_io_num, 1);

    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                 //Len is in bytes, transaction length is in bits.
    t.tx_data[0]=data;               //Data
    t.flags=SPI_TRANS_USE_TXDATA;
    ret=spi_device_polling_transmit(epd_spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

void epdif_send_data(const uint8_t *data, uint32_t len) {
    gpio_set_level(epd_pin_cfg.dc_io_num, 1);

    esp_err_t ret;
    spi_transaction_t t;
    if (len==0) return;             //no need to send anything
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    ret=spi_device_polling_transmit(epd_spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}