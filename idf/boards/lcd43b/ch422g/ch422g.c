/*
 * ch422g.c — ver el encabezado de ch422g.h para por que este archivo existe.
 */
#include "ch422g.h"

static const char *TAG = "ch422g";

static i2c_master_dev_handle_t s_mode   = NULL;
static i2c_master_dev_handle_t s_ioOut  = NULL;
static i2c_master_dev_handle_t s_ioIn   = NULL;
static i2c_master_dev_handle_t s_odOut  = NULL;

// Copia en RAM del byte de salida: el CH422G no se puede releer estando en
// modo salida, asi que cambiar un pin sin pisar los otros exige recordarlo.
static uint8_t s_ioShadow = CH422G_IO_BOOT_VALUE;
static uint8_t s_odShadow = 0x00;

// Escrituras I2C al expansor que fallaron desde el arranque (diagnostico: lo
// lee el log de salud de main.c).
static uint32_t s_errCount = 0;

/*
 * Un byte crudo al esclavo, SIN byte de registro adelante (ver ch422g.h).
 * A diferencia de DEV_I2C_Write_Nbyte(), esto no aborta ante un NACK: una
 * placa que no contesta se diagnostica leyendo el monitor.
 */
static esp_err_t ch422g_write(i2c_master_dev_handle_t dev, uint8_t value, const char *what)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "%s: driver sin inicializar (falta CH422G_Init)", what);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit(dev, &value, 1, 100);
    if (err != ESP_OK) {
        s_errCount++;
        ESP_LOGE(TAG, "%s: fallo la escritura I2C (0x%02x): %s",
                 what, value, esp_err_to_name(err));
    }
    return err;
}

esp_err_t CH422G_Init(void)
{
    DEV_I2C_Set_Slave_Addr(&s_mode,  CH422G_ADDR_MODE);
    DEV_I2C_Set_Slave_Addr(&s_ioOut, CH422G_ADDR_IO_OUT);
    DEV_I2C_Set_Slave_Addr(&s_ioIn,  CH422G_ADDR_IO_IN);
    DEV_I2C_Set_Slave_Addr(&s_odOut, CH422G_ADDR_OD_OUT);

    esp_err_t err = ch422g_write(s_mode, CH422G_MODE_IO_OE, "modo IO_OE");
    if (err != ESP_OK) {
        // Vale la pena gritar: sin el expansor no hay backlight ni touch, y el
        // sintoma (pantalla negra que igual bootea y conecta el WiFi) no dice
        // nada por si solo.
        ESP_LOGE(TAG, "El CH422G no responde en 0x%02x. Si esta placa es una 5B/7B "
                      "lleva el IO_EXTENSION de Waveshare, no un CH422G: usar "
                      "DataBot-LCD7, no este firmware.", CH422G_ADDR_MODE);
        return err;
    }

    s_ioShadow = CH422G_IO_BOOT_VALUE;
    return ch422g_write(s_ioOut, s_ioShadow, "salida IO");
}

esp_err_t CH422G_IO_Output_Raw(uint8_t value)
{
    s_ioShadow = value;
    return ch422g_write(s_ioOut, s_ioShadow, "salida IO");
}

esp_err_t CH422G_IO_Output(uint8_t pin, uint8_t value)
{
    if (pin > 7) return ESP_ERR_INVALID_ARG;
    if (value) s_ioShadow |=  (uint8_t)(1u << pin);
    else       s_ioShadow &= (uint8_t)~(1u << pin);
    return ch422g_write(s_ioOut, s_ioShadow, "salida IO");
}

esp_err_t CH422G_OD_Output(uint8_t pin, uint8_t value)
{
    if (pin > 3) return ESP_ERR_INVALID_ARG;
    if (value) s_odShadow |=  (uint8_t)(1u << pin);
    else       s_odShadow &= (uint8_t)~(1u << pin);

    esp_err_t err = ch422g_write(s_mode, CH422G_MODE_OD_EN, "modo OD_EN");
    if (err != ESP_OK) return err;
    err = ch422g_write(s_odOut, s_odShadow, "salida OD");

    // Volver a modo salida IO: el registro de modo es uno solo y dejarlo en
    // OD_EN apaga las salidas IO0..IO7 (backlight incluido).
    ch422g_write(s_mode, CH422G_MODE_IO_OE, "modo IO_OE");
    return err;
}

uint8_t CH422G_IO_Input(uint8_t pin)
{
    if (pin > 7 || s_ioIn == NULL) return 0;

    // Modo 0 = entrada. Ojo: esto apaga las salidas mientras dure la lectura,
    // por eso el bring-up no usa esta funcion.
    if (ch422g_write(s_mode, 0x00, "modo entrada") != ESP_OK) return 0;

    uint8_t value = 0;
    esp_err_t err = i2c_master_receive(s_ioIn, &value, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lectura IO: %s", esp_err_to_name(err));
        value = 0;
    }

    // Restaurar modo salida y reaplicar la copia en RAM.
    ch422g_write(s_mode, CH422G_MODE_IO_OE, "modo IO_OE");
    ch422g_write(s_ioOut, s_ioShadow, "salida IO");

    return (value & (uint8_t)(1u << pin)) ? 1 : 0;
}

uint8_t CH422G_IO_Shadow(void)
{
    return s_ioShadow;
}

uint32_t CH422G_Error_Count(void)
{
    return s_errCount;
}
