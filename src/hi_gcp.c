/*
 * Google Cloud client: service account JWT (RS256) -> Google ID token -> HTTPS POST.
 * JWT signing follows the approach of nkolban/esp32-snippets (cloud/GCP/JWT), rewritten for ESP-IDF 5 / mbedtls 3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"

#include "hi_gcp.h"
#include "hi_ntp.h"

static const char *TAG = "HI_GCP";

#define TOKEN_URL           "https://oauth2.googleapis.com/token"
#define JWT_LIFETIME_S      3600
#define TOKEN_REFRESH_GAP_S 60
#define JWT_MAX_LEN         1600
#define ID_TOKEN_MAX_LEN    2048
#define RESPONSE_MAX_LEN    4096

static hi_gcp_config_t s_config;
static SemaphoreHandle_t s_lock;
static char s_id_token[ID_TOKEN_MAX_LEN];
static char s_audience[256];
static time_t s_token_exp;

/* base64url without padding; false when out is too small. */
static bool base64url(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char *) out, out_size, &written, in, in_len) != 0) return false;
    while (written > 0 && out[written - 1] == '=') written--;
    out[written] = '\0';
    for (size_t i = 0; i < written; i++) {
        if (out[i] == '+') out[i] = '-';
        else if (out[i] == '/') out[i] = '_';
    }
    return true;
}

static void http_client_config_ca(esp_http_client_config_t *http)
{
    if (s_config.ca_cert_pem != NULL) {
        http->cert_pem = s_config.ca_cert_pem;
    } else {
        http->crt_bundle_attach = esp_crt_bundle_attach;
    }
}

/* Signed JWT asking Google for an ID token with target_audience; false on any error. */
static bool create_jwt(const char *audience, time_t now, char *out, size_t out_size)
{
    char header_b64[64];
    static const char header[] = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
    char payload[512];
    char payload_b64[720];
    int n = snprintf(payload, sizeof(payload),
                     "{\"iat\":%lld,\"exp\":%lld,\"aud\":\"%s\",\"target_audience\":\"%s\",\"iss\":\"%s\",\"sub\":\"%s\"}",
                     (long long) now, (long long) now + JWT_LIFETIME_S, TOKEN_URL, audience,
                     s_config.service_account_email, s_config.service_account_email);
    if (n <= 0 || n >= (int) sizeof(payload)
        || !base64url((const uint8_t *) header, strlen(header), header_b64, sizeof(header_b64))
        || !base64url((const uint8_t *) payload, (size_t) n, payload_b64, sizeof(payload_b64))) {
        ESP_LOGE(TAG, "JWT too large");
        return false;
    }
    n = snprintf(out, out_size, "%s.%s", header_b64, payload_b64);
    if (n <= 0 || n >= (int) out_size) return false;
    size_t signing_input_len = (size_t) n;

    bool ok = false;
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    uint8_t digest[32];
    uint8_t signature[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t sig_len = 0;
    char sig_b64[700];
    int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *) "hi_gcp", 6);
    if (rc == 0) {
        rc = mbedtls_pk_parse_key(&pk, (const unsigned char *) s_config.private_key_pem,
                                  strlen(s_config.private_key_pem) + 1, NULL, 0, mbedtls_ctr_drbg_random, &drbg);
    }
    if (rc == 0) {
        rc = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const unsigned char *) out,
                        signing_input_len, digest);
    }
    if (rc == 0) {
        rc = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, sizeof(signature), &sig_len,
                             mbedtls_ctr_drbg_random, &drbg);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "JWT signing failed: -0x%04x", (unsigned) -rc);
    } else if (base64url(signature, sig_len, sig_b64, sizeof(sig_b64))
               && snprintf(out + signing_input_len, out_size - signing_input_len, ".%s", sig_b64)
                  < (int) (out_size - signing_input_len)) {
        ok = true;
    }

    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

/* Performs a POST and reads up to response_size-1 bytes of the body. Returns HTTP status or -1. */
static int http_post(const char *url, const char *content_type, const char *authorization, const char *body,
                     char *response, size_t response_size)
{
    esp_http_client_config_t http = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .buffer_size_tx = 2048,
    };
    http_client_config_ca(&http);
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (client == NULL) return -1;
    esp_http_client_set_header(client, "Content-Type", content_type);
    if (authorization != NULL) esp_http_client_set_header(client, "Authorization", authorization);

    int status = -1;
    size_t body_len = strlen(body);
    esp_err_t err = esp_http_client_open(client, (int) body_len);
    if (err == ESP_OK && esp_http_client_write(client, body, (int) body_len) == (int) body_len
        && esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        if (response != NULL && response_size > 0) {
            int read = esp_http_client_read_response(client, response, (int) response_size - 1);
            response[read > 0 ? read : 0] = '\0';
        }
    } else {
        ESP_LOGW(TAG, "POST %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

/* Refreshes s_id_token for audience when missing, expiring or for another audience. Caller holds s_lock. */
static bool ensure_token_locked(const char *audience)
{
    time_t now = time(NULL);
    if (s_id_token[0] != '\0' && strcmp(s_audience, audience) == 0 && now + TOKEN_REFRESH_GAP_S < s_token_exp) {
        return true;
    }
    if (!hi_ntp_synced()) {
        ESP_LOGW(TAG, "Clock not synced: cannot create a JWT");
        return false;
    }

    char *jwt = malloc(JWT_MAX_LEN);
    char *form = malloc(JWT_MAX_LEN + 96);
    char *response = malloc(RESPONSE_MAX_LEN);
    bool ok = false;
    if (jwt && form && response && create_jwt(audience, now, jwt, JWT_MAX_LEN)) {
        snprintf(form, JWT_MAX_LEN + 96, "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=%s", jwt);
        int status = http_post(TOKEN_URL, "application/x-www-form-urlencoded", NULL, form, response, RESPONSE_MAX_LEN);
        cJSON *root = status == 200 ? cJSON_Parse(response) : NULL;
        const cJSON *id_token = cJSON_GetObjectItemCaseSensitive(root, "id_token");
        if (cJSON_IsString(id_token) && strlen(id_token->valuestring) < sizeof(s_id_token)) {
            strlcpy(s_id_token, id_token->valuestring, sizeof(s_id_token));
            strlcpy(s_audience, audience, sizeof(s_audience));
            s_token_exp = now + JWT_LIFETIME_S;
            ok = true;
        } else {
            ESP_LOGW(TAG, "Token exchange failed (HTTP %d)", status);
        }
        cJSON_Delete(root);
    }
    free(jwt);
    free(form);
    free(response);
    if (!ok) s_id_token[0] = '\0';
    return ok;
}

esp_err_t hi_gcp_init(const hi_gcp_config_t *config)
{
    if (config->service_account_email == NULL || config->private_key_pem == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t hi_gcp_post_json(const char *url, const char *json, int *http_status)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (http_status) *http_status = -1;
    if (strlen(url) >= sizeof(s_audience)) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    char authorization[ID_TOKEN_MAX_LEN + 8];
    bool have_token = ensure_token_locked(url);
    if (have_token) snprintf(authorization, sizeof(authorization), "Bearer %s", s_id_token);
    xSemaphoreGive(s_lock);
    if (!have_token) return ESP_FAIL;

    int status = http_post(url, "application/json", authorization, json, NULL, 0);
    if (http_status) *http_status = status;
    if (status == 401 || status == 403) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_id_token[0] = '\0';   // force a new token next time
        xSemaphoreGive(s_lock);
    }
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}
