/*
 * Vendorizado de examples/ESP-IDF/13_lvgl_v8_demo/components/gpio del repo
 * oficial waveshareteam/ESP32-S3-Touch-LCD-7B, sin cambios.
 */
#include "gpio.h"

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = 1ULL << Pin;

    if (Mode == 0 || Mode == GPIO_MODE_INPUT) {
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    } else if (Mode == GPIO_MODE_INPUT_OUTPUT) {
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    } else {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    }

    gpio_config(&io_conf);
}

void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = 1ULL << Pin;

    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(Pin, isr_handler, (void *)Pin);
}

void DEV_Digital_Write(uint16_t Pin, uint8_t Value)
{
    gpio_set_level(Pin, Value);
}

uint8_t DEV_Digital_Read(uint16_t Pin)
{
    return gpio_get_level(Pin);
}
