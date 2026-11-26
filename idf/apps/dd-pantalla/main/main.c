/*
 * dd-pantalla — firmware de prueba DataDruida para la Waveshare
 * ESP32-S3-Touch-LCD-4.3B (800x480, GT911, expansor CH422G).
 *
 * Es lo primero que ve quien compra la placa: la conecta, elige su WiFi en
 * la pantalla con el teclado táctil, y desde ahí todo llega por OTA. No hace
 * falta USB en ningún momento.
 *
 * El bring-up del panel es el de DataBot-LCD4.3 (verificado en hardware):
 * ⚠ en esta placa GPIO0 (BOOT) es una línea de datos del LCD, así que el modo
 * seguro por botón no existe; queda el de "3 reinicios por error seguidos".
 */
#include <assert.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "rgb_lcd_port.h"
#include "dd_core.h"
#include "ui.h"

static const char *TAG = "dd-pantalla";

void app_main(void)
{
    dd_core_config_t cfg = DD_CORE_DEFAULT_CONFIG("dd-pantalla-43b");
    cfg.board_id = "waveshare-esp32-s3-touch-lcd-4.3b";
    cfg.has_screen = true;
    cfg.open_ap_if_unset = false;   // el WiFi se elige en la pantalla
    cfg.boot_button_gpio = -1;      // GPIO0 = dato G3 del LCD
    cfg.auto_update_default = false;// la pantalla avisa y el usuario decide
    ESP_ERROR_CHECK(dd_core_start(&cfg));

    const esp_lv_adapter_rotation_t rot = ESP_LV_ADAPTER_ROTATE_0;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB, rot, &panel, &touch));
    waveshare_rgb_lcd_bl_on();

    esp_lv_adapter_config_t ac = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ac.task_stack_size = 12 * 1024;   // igual que DataBot-LCD4.3
    ac.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&ac));

    esp_lv_adapter_display_config_t dc = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rot);
    dc.profile.use_psram = true;
    // ⚠ 20 líneas: el draw buffer va SIEMPRE a RAM interna; con más, el TLS
    // de la OTA se queda sin memoria (ver DataBot-LCD4.3/main/main.c).
    dc.profile.buffer_height = 20;
    lv_display_t *disp = esp_lv_adapter_register_display(&dc);
    assert(disp);
    if (touch) {
        esp_lv_adapter_touch_config_t tc = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
        assert(esp_lv_adapter_register_touch(&tc));
    } else {
        ESP_LOGE(TAG, "El touch GT911 no respondió");
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_build();
        esp_lv_adapter_unlock();
    }
    ESP_LOGI(TAG, "RAM interna libre: %u KB, PSRAM libre: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
