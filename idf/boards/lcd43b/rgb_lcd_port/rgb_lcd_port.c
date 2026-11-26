/*
 * rgb_lcd_port.c — bring-up del panel RGB 800x480 + GT911 de la 4.3B.
 * Ver el encabezado de rgb_lcd_port.h para el diff contra la 7B.
 */
#include "rgb_lcd_port.h"

#include "gpio.h"
#include "i2c.h"

static const char *TAG = "rgb_lcd";

static esp_lcd_panel_handle_t panel_handle = NULL;

/*
 * Reset del GT911 y seleccion de su direccion I2C.
 *
 * El GT911 mira el nivel de su pin INT mientras sale de reset para elegir
 * direccion: INT bajo -> 0x5D (la que asume ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG),
 * INT alto -> 0x14. Por eso hay que manejar el INT como salida DURANTE la
 * secuencia, aunque despues sea una entrada.
 *
 * Traduccion 7B -> 4.3B: donde la 7B escribe registros del IO_EXTENSION, aca
 * se escribe el byte de salida completo del CH422G. Los valores 0x2C/0x2E del
 * ejemplo oficial de Waveshare son exactamente "backlight+resets arriba, IO1
 * abajo" y despues "IO1 arriba", que es lo que expresan estas dos llamadas.
 */
static void touch_reset_sequence(void)
{
    // CH422G_Init() ya dejo la salida en CH422G_IO_BOOT_VALUE (0x2C): el GT911
    // esta en reset (IO1 bajo) desde antes de llegar aca.
    DEV_GPIO_Mode(EXAMPLE_TOUCH_INT_GPIO, GPIO_MODE_INPUT_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    DEV_Digital_Write(EXAMPLE_TOUCH_INT_GPIO, 0);    // -> direccion 0x5D
    vTaskDelay(pdMS_TO_TICKS(100));

    CH422G_IO_Output(CH422G_IO_1, 1);                // soltar el reset (0x2C -> 0x2E)
    vTaskDelay(pdMS_TO_TICKS(200));

#if !DB_TOUCH_USE_INT
    /*
     * Mejora sobre el ejemplo de Waveshare: aquel deja GPIO4 como salida en
     * bajo para siempre, peleandole al GT911 que quiere usar ese mismo pin
     * como salida de interrupcion. En modo polling no necesitamos el pin para
     * nada, asi que se suelta como entrada y se termina la contienda.
     */
    DEV_GPIO_Mode(EXAMPLE_TOUCH_INT_GPIO, GPIO_MODE_INPUT);
#endif
}

esp_err_t waveshare_esp32_s3_rgb_lcd_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                                          esp_lv_adapter_rotation_t rotation,
                                          esp_lcd_panel_handle_t *out_panel_handle,
                                          esp_lcd_touch_handle_t *out_touch_handle)
{
    if ((out_panel_handle == NULL) || (out_touch_handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_panel_handle = NULL;
    *out_touch_handle = NULL;

    const uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);

    ESP_LOGI(TAG, "Panel RGB %dx%d @ %d MHz", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             (int)(EXAMPLE_LCD_PIXEL_CLOCK_HZ / 1000000));

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            .hsync_pulse_width = EXAMPLE_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch  = EXAMPLE_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = EXAMPLE_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = EXAMPLE_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch  = EXAMPLE_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = EXAMPLE_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL,
        .num_fbs = num_fbs,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    *out_panel_handle = panel_handle;

    // ── Expansor + touch ────────────────────────────────────────────
    DEV_I2C_Port port = DEV_I2C_Init();

    // Si el CH422G no contesta no tiene sentido seguir con el touch: no hay
    // backlight ni forma de sacar al GT911 del reset. Se sigue igual para que
    // el equipo bootee (WiFi/MQTT andan a ciegas) pero el log ya grito.
    esp_err_t exp_err = CH422G_Init();

    touch_reset_sequence();

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_I2C_MASTER_FREQUENCY;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(port.bus, &tp_io_config, &tp_io_handle));

    // ⚠ esp_lv_adapter rota el framebuffer pero NO las coordenadas del touch:
    // mirror_x/mirror_y se derivan de la rotacion o los taps caen en el punto
    // opuesto. Mismos valores que touch_rotation_helper.c del adapter (caso
    // no-MIPI). Solo 0 y 180: la placa se usa apaisada en ambos sentidos.
    const bool rot180 = (rotation == ESP_LV_ADAPTER_ROTATE_180);
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_TOUCH_RST_GPIO,
#if DB_TOUCH_USE_INT
        .int_gpio_num = EXAMPLE_TOUCH_INT_GPIO,
#else
        .int_gpio_num = -1,
#endif
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = rot180,
            .mirror_y = rot180,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, out_touch_handle));

    return exp_err;
}

void waveshare_get_frame_buffer(void **buf1, void **buf2)
{
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, buf1, buf2));
}

void waveshare_rgb_lcd_set_pclk(uint32_t freq_hz)
{
    esp_lcd_rgb_panel_set_pclk(panel_handle, freq_hz);
}

void waveshare_rgb_lcd_restart(void)
{
    esp_lcd_rgb_panel_restart(panel_handle);
}

// El CH422G no tiene PWM (el IO_EXTENSION de la 7B si): el backlight de esta
// placa es on/off y nada mas. Si alguna vez hace falta regularlo, hay que
// hacerlo por software (bajando el brillo de los colores) o cablear un LEDC.
void waveshare_rgb_lcd_bl_on(void)
{
    CH422G_IO_Output(CH422G_IO_2, 1);
}

void waveshare_rgb_lcd_bl_off(void)
{
    CH422G_IO_Output(CH422G_IO_2, 0);
}
