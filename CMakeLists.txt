set(srcs
    "src/hi_system.c"
    "src/hi_wifi.c"
    "src/hi_log.c"
    "src/hi_ntp.c"
    "src/hi_ota.c"
    "src/hi_notify.c"
    "src/hi_health.c"
    "src/hi_auth.c"
    "src/hi_httpd.c")

if(CONFIG_HOME_IDF_GCP)
    list(APPEND srcs "src/hi_gcp.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_timer json
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_ringbuf nvs_flash esp_http_client esp_https_ota esp_app_format
                  app_update mbedtls lwip)
