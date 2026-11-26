/*
 * rgb_lcd_port.h — Panel RGB + touch GT911 de la Waveshare
 * ESP32-S3-Touch-LCD-4.3B (800x480).
 *
 * Portado del rgb_lcd_port de ..\DataBot-LCD7 (que a su vez viene del ejemplo
 * 13_lvgl_v8_demo de la 7B) cruzando los datos de la placa 4.3B con su ejemplo
 * oficial 09_lvgl_v9_demo.
 *
 * ✅ LO QUE NO CAMBIA respecto de la 7B: los 16 pines de datos RGB y los cuatro
 *    de sincronismo son EXACTAMENTE los mismos GPIO, igual que SDA/SCL (8/9) y
 *    TP_IRQ (GPIO4). Toda la familia ESP32-S3-Touch-LCD comparte el pinout.
 *
 * ⚠ LO QUE SI CAMBIA, y es todo lo que importa:
 *    1. Resolucion 800x480 (no 1024x600) y timings/pclk propios del panel.
 *    2. El expansor es un CH422G, no el IO_EXTENSION de la 7B — ver ch422g.h.
 *       De ahi sale el reset del touch y el backlight.
 *    3. La secuencia de reset del GT911 se escribe en las direcciones I2C del
 *       CH422G (0x24/0x38), no en registros de 0x24.
 */
#ifndef _RGB_LCD_H_
#define _RGB_LCD_H_

#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "ch422g.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lv_adapter.h"

#define EXAMPLE_LCD_H_RES               (800)
#define EXAMPLE_LCD_V_RES               (480)

/*
 * 16 MHz y porches 4/8/8 son los valores del ejemplo oficial de la 4.3B
 * (09_lvgl_v9_demo, rama ESP_PANEL_USE_1024_600_LCD = 0). Son mucho mas
 * chicos que los de la 7B (30,85 MHz, porches de 3 cifras) porque es otro
 * panel: no interpolar ni "promediar" entre placas.
 *
 * Si la imagen sale corrida, con franjas o parpadeando, esto es lo primero a
 * tocar — y conviene tocarlo contra el ejemplo oficial, no a ojo.
 */
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ      (16 * 1000 * 1000)
#define EXAMPLE_LCD_HSYNC_PULSE_WIDTH   (4)
#define EXAMPLE_LCD_HSYNC_BACK_PORCH    (8)
#define EXAMPLE_LCD_HSYNC_FRONT_PORCH   (8)
#define EXAMPLE_LCD_VSYNC_PULSE_WIDTH   (4)
#define EXAMPLE_LCD_VSYNC_BACK_PORCH    (8)
#define EXAMPLE_LCD_VSYNC_FRONT_PORCH   (8)

#define EXAMPLE_LCD_BIT_PER_PIXEL       (16)
#define EXAMPLE_RGB_BIT_PER_PIXEL       (16)
#define EXAMPLE_RGB_DATA_WIDTH          (16)
#define EXAMPLE_LCD_RGB_BUFFER_NUMS     (2)
#define EXAMPLE_RGB_BOUNCE_BUFFER_SIZE  (EXAMPLE_LCD_H_RES * 10)

#define EXAMPLE_LCD_IO_RGB_DISP         (-1)
#define EXAMPLE_LCD_IO_RGB_VSYNC        (GPIO_NUM_3)
#define EXAMPLE_LCD_IO_RGB_HSYNC        (GPIO_NUM_46)
#define EXAMPLE_LCD_IO_RGB_DE           (GPIO_NUM_5)
#define EXAMPLE_LCD_IO_RGB_PCLK         (GPIO_NUM_7)

#define EXAMPLE_LCD_IO_RGB_DATA0        (GPIO_NUM_14) ///< B3
#define EXAMPLE_LCD_IO_RGB_DATA1        (GPIO_NUM_38) ///< B4
#define EXAMPLE_LCD_IO_RGB_DATA2        (GPIO_NUM_18) ///< B5
#define EXAMPLE_LCD_IO_RGB_DATA3        (GPIO_NUM_17) ///< B6
#define EXAMPLE_LCD_IO_RGB_DATA4        (GPIO_NUM_10) ///< B7

#define EXAMPLE_LCD_IO_RGB_DATA5        (GPIO_NUM_39) ///< G2
#define EXAMPLE_LCD_IO_RGB_DATA6        (GPIO_NUM_0)  ///< G3
#define EXAMPLE_LCD_IO_RGB_DATA7        (GPIO_NUM_45) ///< G4
#define EXAMPLE_LCD_IO_RGB_DATA8        (GPIO_NUM_48) ///< G5
#define EXAMPLE_LCD_IO_RGB_DATA9        (GPIO_NUM_47) ///< G6
#define EXAMPLE_LCD_IO_RGB_DATA10       (GPIO_NUM_21) ///< G7

#define EXAMPLE_LCD_IO_RGB_DATA11       (GPIO_NUM_1)  ///< R3
#define EXAMPLE_LCD_IO_RGB_DATA12       (GPIO_NUM_2)  ///< R4
#define EXAMPLE_LCD_IO_RGB_DATA13       (GPIO_NUM_42) ///< R5
#define EXAMPLE_LCD_IO_RGB_DATA14       (GPIO_NUM_41) ///< R6
#define EXAMPLE_LCD_IO_RGB_DATA15       (GPIO_NUM_40) ///< R7

#define EXAMPLE_LCD_IO_RST              (-1)
#define EXAMPLE_PIN_NUM_BK_LIGHT        (-1)   // el backlight cuelga del CH422G

// TP_IRQ del GT911. Se usa como SALIDA durante el reset (elige la direccion
// I2C 0x5D del GT911) y despues se suelta.
#define EXAMPLE_TOUCH_INT_GPIO          (GPIO_NUM_4)
#define EXAMPLE_TOUCH_RST_GPIO          (-1)   // el reset cuelga del CH422G (IO1)

/*
 * Modo de lectura del touch:
 *   0 (default) = POLLING. El pin TP_IRQ queda como entrada libre y el driver
 *       consulta al GT911 por I2C en cada refresco del indev. Es lo que hace
 *       el ejemplo oficial de la 4.3B (pasa int_gpio_num = -1) y por eso es el
 *       default acá: esta placa todavia no esta verificada por nosotros.
 *   1 = INTERRUPCION. Le pasa GPIO4 al driver como int_gpio_num, igual que en
 *       la 7B (donde SI esta verificado en hardware). Ahorra trafico I2C.
 * Si el touch no responde, probar el otro valor antes que cualquier otra cosa.
 */
#define DB_TOUCH_USE_INT                (0)

esp_err_t waveshare_esp32_s3_rgb_lcd_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                                          esp_lv_adapter_rotation_t rotation,
                                          esp_lcd_panel_handle_t *panel_handle,
                                          esp_lcd_touch_handle_t *touch_handle);
void waveshare_rgb_lcd_bl_on(void);
void waveshare_rgb_lcd_bl_off(void);
void waveshare_get_frame_buffer(void **buf1, void **buf2);
void waveshare_rgb_lcd_set_pclk(uint32_t freq_hz);
void waveshare_rgb_lcd_restart(void);
#endif // _RGB_LCD_H_
