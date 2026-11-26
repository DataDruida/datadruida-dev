/*
 * ch422g.h — Expansor I/O de la Waveshare ESP32-S3-Touch-LCD-4.3B.
 *
 * ⚠ ESTE ES EL ARCHIVO QUE SEPARA ESTA PLACA DE LA 7B.
 *
 * La 7B (y la 5B) llevan el "IO_EXTENSION" propio de Waveshare: UNA sola
 * direccion I2C (0x24) con registros internos direccionados dentro del mismo
 * paquete [registro, dato].  La 4.3B lleva un CH422G de verdad, que funciona
 * al reves: NO tiene registros — cada "registro" ES una direccion I2C distinta
 * y se le escribe UN SOLO BYTE crudo, sin byte de registro adelante:
 *
 *     0x24 -> registro de modo        (IO_OE / OD_EN / SLEEP / A_SCAN)
 *     0x38 -> salida de los 8 pines IO0..IO7
 *     0x26 -> lectura de los 8 pines IO0..IO7
 *     0x23 -> salida open-drain OC0..OC3
 *
 * Cargar el driver de la otra familia no "anda a medias": la pantalla no
 * arranca (nunca se suelta el reset del touch ni se prende el backlight).
 * Es exactamente el mismo pozo de debug que documenta ..\DataBot-LCD7 al
 * reves — alla la libreria Arduino asumia CH422G y el chip era IO_EXTENSION.
 *
 * Referencia: waveshareteam/ESP32-S3-Touch-LCD-4.3B,
 * examples/ESP-IDF/05_IO_Test/lib/CH422G/. Aquel driver usa la API I2C vieja
 * (driver/i2c.h) y aborta en silencio ante un NACK; este esta portado a la API
 * i2c_master de ESP-IDF 5.x que ya usa el resto del proyecto y LOGUEA el error
 * en vez de tirar un ESP_ERROR_CHECK (una placa que no contesta se diagnostica
 * leyendo el monitor, no adivinando por que arranco en blanco).
 */
#ifndef __CH422G_H
#define __CH422G_H

#include "i2c.h"

// Las cuatro direcciones I2C del chip (no son registros: son esclavos I2C).
#define CH422G_ADDR_MODE    0x24
#define CH422G_ADDR_OD_OUT  0x23
#define CH422G_ADDR_IO_IN   0x26
#define CH422G_ADDR_IO_OUT  0x38

// Bits del registro de modo (0x24).
#define CH422G_MODE_IO_OE   0x01   // habilita la salida de IO0..IO7
#define CH422G_MODE_A_SCAN  0x02   // barrido automatico de display (sin uso)
#define CH422G_MODE_OD_EN   0x04   // habilita open-drain en OC0..OC3
#define CH422G_MODE_SLEEP   0x08   // bajo consumo

/*
 * ⚠ Estos son INDICES de pin (0..7), NO mascaras de bits — igual que el
 * IO_EXTENSION_IO_n de la 7B, para que el codigo de bring-up se lea igual en
 * las dos placas. El CH422G.h de Waveshare define los mismos nombres como
 * MASCARAS (0x01/0x02/0x04...). Si alguna vez se copia y pega codigo de aquel
 * ejemplo, convertir: mascara = (1 << indice).
 *
 * El mapa de pines es el mismo que en la 7B (misma familia de placas).
 */
#define CH422G_IO_0         0      // DI0 aislada
#define CH422G_IO_1         1      // reset del touch GT911
#define CH422G_IO_2         2      // backlight (on/off — el CH422G no tiene PWM)
#define CH422G_IO_3         3      // reset del LCD
#define CH422G_IO_4         4      // CS de la microSD
#define CH422G_IO_5         5      // DI1 aislada / seleccion USB-CAN
#define CH422G_IO_6         6
#define CH422G_IO_7         7

/*
 * Estado de salida con el que arranca la placa, copiado tal cual de la
 * secuencia verificada por Waveshare (05_IO_Test y el demo LVGL):
 * IO2 (backlight) + IO3 (reset de LCD liberado) + IO5 en alto, IO1 en BAJO
 * para dejar el GT911 en reset hasta que rgb_lcd_port.c corra la secuencia de
 * seleccion de direccion.
 */
#define CH422G_IO_BOOT_VALUE  0x2C

// Da de alta los cuatro esclavos I2C, pone el chip en modo salida y aplica
// CH422G_IO_BOOT_VALUE. Llamar despues de DEV_I2C_Init().
esp_err_t CH422G_Init(void);

// value: 0 = bajo, !=0 = alto. Mantiene una copia en RAM del byte de salida
// (el chip no se puede leer de vuelta en modo salida).
esp_err_t CH422G_IO_Output(uint8_t pin, uint8_t value);

// Escribe los 8 bits de salida de una sola vez.
esp_err_t CH422G_IO_Output_Raw(uint8_t value);

// Lee un pin. Devuelve 0/1; ante error de bus devuelve 0 y loguea.
uint8_t CH422G_IO_Input(uint8_t pin);

// Salidas open-drain OC0..OC3 (pin 0..3). Sin uso en esta placa, se expone por
// completitud del driver.
esp_err_t CH422G_OD_Output(uint8_t pin, uint8_t value);

// Diagnostico: ultimo byte de salida que se le escribio (no se relee del chip,
// que en modo salida no se puede leer sin apagar las salidas) y cantidad de
// escrituras I2C fallidas desde el arranque.
uint8_t  CH422G_IO_Shadow(void);
uint32_t CH422G_Error_Count(void);

#endif
