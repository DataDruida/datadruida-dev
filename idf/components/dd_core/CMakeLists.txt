idf_component_register(
    SRCS "dd_core.c" "dd_wifi.c" "dd_dns.c" "dd_ota.c" "dd_web.c" "dd_cloud.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_server
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_timer esp_http_client
                  esp_https_ota app_update esp_app_format mbedtls json lwip esp_driver_gpio
    EMBED_TXTFILES "web/index.html"
)
