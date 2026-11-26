/*
 * dd-reles — firmware DataDruida para las placas Waveshare SIN pantalla
 * (ESP32-S3-ETH-8DI-8RO, ESP32-S3-Relay-6CH, ESP32-S3-Relay-1CH).
 *
 * Un solo binario para las tres: la placa se elige una vez en el panel
 * (http://dd-xxxxxx.local) y queda en NVS. Todo lo de placa, relés, sensores
 * y el programa del usuario vive en el componente dd_app; el programa en sí
 * es main/app.c (dd_setup / dd_loop, ver components/dd_app/include/dd.h).
 *
 * Mantener BOOT apretado 3 s al encender = modo seguro (reabre el portal y
 * no corre el programa).
 */
#include "dd_core.h"
#include "dd_app.h"

void app_main(void)
{
    dd_core_config_t cfg = DD_CORE_DEFAULT_CONFIG("dd-reles");
    dd_app_core_hooks(&cfg);
    ESP_ERROR_CHECK(dd_core_start(&cfg));
    ESP_ERROR_CHECK(dd_app_start());
}
