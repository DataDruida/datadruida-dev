/*
 * dd_app.h — para los firmwares base (no para el programa del usuario).
 *
 *     dd_core_config_t cfg = DD_CORE_DEFAULT_CONFIG("dd-reles");
 *     dd_app_core_hooks(&cfg);   // rutas del panel: relés, log
 *     dd_core_start(&cfg);
 *     dd_app_start();            // corre dd_setup()/dd_loop() (no en modo seguro)
 */
#pragma once
#include "esp_err.h"
#include "dd_core.h"

void dd_app_core_hooks(dd_core_config_t *cfg);
esp_err_t dd_app_start(void);
