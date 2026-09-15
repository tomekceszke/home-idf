#pragma once

/* Optional (CONFIG_HOME_IDF_GCP): Google Cloud client for devices without a cloud IoT broker.
 * Service account JWT (RS256) -> Google ID token (cached per audience) -> HTTPS POST with Bearer token.
 * Blocking network I/O: call from a background task, never from time-critical code. */

#include <stddef.h>
#include "esp_err.h"

typedef struct {
    const char *service_account_email;
    const char *private_key_pem;        // PKCS#8 / PKCS#1 RSA key, NUL-terminated
    const char *ca_cert_pem;            // trust anchor for googleapis.com and the endpoint; NULL = crt bundle
} hi_gcp_config_t;

esp_err_t hi_gcp_init(const hi_gcp_config_t *config);

/* POSTs json to url (the audience of the ID token). http_status (optional) receives the HTTP status. */
esp_err_t hi_gcp_post_json(const char *url, const char *json, int *http_status);
