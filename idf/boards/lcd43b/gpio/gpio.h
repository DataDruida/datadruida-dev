/*
 * Vendorizado de examples/ESP-IDF/13_lvgl_v8_demo/components/gpio del repo
 * oficial waveshareteam/ESP32-S3-Touch-LCD-7B, sin cambios.
 */
#ifndef __GPIO_H
#define __GPIO_H

#include "driver/gpio.h"

#define LED_GPIO_PIN    GPIO_NUM_6

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode);
void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler);
void DEV_Digital_Write(uint16_t Pin, uint8_t Value);
uint8_t DEV_Digital_Read(uint16_t Pin);

#endif
