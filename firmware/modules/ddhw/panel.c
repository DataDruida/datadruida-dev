// ddhw/panel.c — paneles RGB y SPI con esp_lcd + luz de fondo
// Licencia Apache-2.0 — DataDruida

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ddhw.h"

// lo define modddhw.c: avisa a LVGL que terminó una transferencia SPI
bool ddhw_spi_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx);

static esp_err_t rgb_init(const dd_board_t *b, esp_lcd_panel_handle_t *out) {
    esp_lcd_rgb_panel_config_t c = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = b->pclk_hz,
            .h_res = b->w,
            .v_res = b->h,
            .hsync_pulse_width = b->hpw,
            .hsync_back_porch = b->hbp,
            .hsync_front_porch = b->hfp,
            .vsync_pulse_width = b->vpw,
            .vsync_back_porch = b->vbp,
            .vsync_front_porch = b->vfp,
            .flags = { .pclk_active_neg = 1 },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = b->w * 10,   // evita el "drift" de la imagen con PSRAM
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = b->hsync,
        .vsync_gpio_num = b->vsync,
        .de_gpio_num = b->de,
        .pclk_gpio_num = b->pclk,
        .disp_gpio_num = -1,
        .flags = { .fb_in_psram = 1 },
    };
    for (int i = 0; i < 16; i++) {
        c.data_gpio_nums[i] = b->data[i];
    }
    esp_err_t e = esp_lcd_new_rgb_panel(&c, out);
    if (e == ESP_OK) {
        e = esp_lcd_panel_init(*out);
    }
    return e;
}

static esp_err_t spi_init(const dd_board_t *b, esp_lcd_panel_handle_t *out) {
    spi_bus_config_t bus = {
        .sclk_io_num = b->sck,
        .mosi_io_num = b->mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = b->w * 40 * 2,
    };
    esp_err_t e = spi_bus_initialize((spi_host_device_t)b->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t ioc = {
        .dc_gpio_num = b->dc,
        .cs_gpio_num = b->cs,
        .pclk_hz = b->spi_hz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = ddhw_spi_done,
        .user_ctx = NULL,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)b->spi_host, &ioc, &io);
    if (e != ESP_OK) {
        return e;
    }
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = b->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    e = esp_lcd_new_panel_st7789(io, &pc, out);
    if (e != ESP_OK) {
        return e;
    }
    esp_lcd_panel_reset(*out);
    esp_lcd_panel_init(*out);
    esp_lcd_panel_invert_color(*out, b->invert);
    esp_lcd_panel_disp_on_off(*out, true);
    return ESP_OK;
}

esp_err_t dd_panel_init(const dd_board_t *b, esp_lcd_panel_handle_t *out) {
    esp_err_t e = dd_i2c_init(b->sda, b->scl, b->i2c_hz);
    if (e != ESP_OK) {
        return e;
    }
    if (b->expander != DD_EXP_NONE) {
        dd_exp_init(b->expander);           // backlight + LCD fuera de reset, touch en reset
    }
    e = (b->bus == DD_BUS_RGB) ? rgb_init(b, out) : spi_init(b, out);
    if (e != ESP_OK) {
        return e;
    }
    dd_touch_init(b);
    dd_backlight(b, 100);
    return ESP_OK;
}

void dd_backlight(const dd_board_t *b, int pct) {
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    if (b->bl >= 0) {
        static bool ready = false;
        if (!ready) {
            ledc_timer_config_t t = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .duty_resolution = LEDC_TIMER_10_BIT,
                .timer_num = LEDC_TIMER_3,
                .freq_hz = 5000,
                .clk_cfg = LEDC_AUTO_CLK,
            };
            ledc_timer_config(&t);
            ledc_channel_config_t ch = {
                .gpio_num = b->bl,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = LEDC_CHANNEL_7,
                .timer_sel = LEDC_TIMER_3,
                .duty = 0,
            };
            ledc_channel_config(&ch);
            ready = true;
        }
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, (uint32_t)(pct * 1023 / 100));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    } else if (b->expander != DD_EXP_NONE) {
        // CH422G: sólo prendido/apagado. IO_EXTENSION (7B): además tiene PWM.
        dd_exp_out(b->expander, 2, pct > 0);
        if (b->expander == DD_EXP_IOEXT && pct > 0) {
            int v = pct > 97 ? 97 : pct;
            uint8_t pwm[2] = { 0x05, (uint8_t)(v * 255 / 100) };
            dd_i2c_write(0x24, pwm, 2);
        }
    }
}
