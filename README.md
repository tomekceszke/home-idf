# home-idf

Shared ESP-IDF framework for my home controllers: water anti-flood valve, heating monitor, floor-heating pump,
garage gate with camera. It contains the parts every device needs and that used to be copied (and drift) between
projects. It was hardened on devices running 24/7.

| Module | What it does |
|---|---|
| `hi_system` | NVS init (reports erasure), reset reason, delayed restart |
| `hi_wifi` | Station that joins the **strongest** AP of an SSID (all-channel scan, 802.11k/v); reconnect with backoff on an `esp_timer`; never blocks boot or the event loop |
| `hi_log` | `esp_log` to serial + UDP, `(HH:MM:SS)` timestamps once the clock is set, error-line hook |
| `hi_ntp` | Non-blocking SNTP, time zone, boot time |
| `hi_ota` | HTTPS OTA from a local server with a pinned certificate, in a background task |
| `hi_health` | Verifies a new image (app-defined health predicate, reset counting that ignores brownouts) and rolls back to the previous slot |
| `hi_notify` | [ntfy](https://ntfy.sh) push notifications through a queue: callers never block, errors are de-duplicated |
| `hi_auth` | Web login: PBKDF2-SHA256 password, sessions persisted in NVS (hash only), CSRF tokens (HMAC), login back-off, admin header for scripts |
| `hi_httpd` | HTTP server with guards: Host allowlist (DNS rebinding), Origin + CSRF + JSON for mutations, CSP, common session/UI/admin routes |
| `hi_gcp` | *Optional* (`CONFIG_HOME_IDF_GCP`): service account JWT → Google ID token → HTTPS POST to Cloud Functions / Cloud Run |

## Design rules

- **Nothing blocks the caller.** Network work (notifications, OTA, reconnects) runs in its own tasks, timers or queues.
  Application control loops never wait for the network.
- **Survive bad power.** A brownout during the first boot of a new image must not roll it back, but an image that keeps crashing must.
- **Secure by default on a LAN.** No credentials in markup, no CORS, no state change without CSRF, constant-time comparisons.
- **Configuration at runtime.** Structs passed to `*_init` carry per-device values; Kconfig (`menuconfig` → *home-idf*) holds tunables.
  Secrets stay in the application (`credentials.h`, never committed).

## Usage

`main/idf_component.yml`:

```yaml
dependencies:
  home-idf:
    git: https://github.com/tomekceszke/home-idf.git
    version: v0.1.0
    # override_path: ../../home-idf     # local development
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c" PRIV_REQUIRES nvs_flash)
home_idf_embed_gzip(${COMPONENT_LIB} ${project_dir}/web/login.html ${project_dir}/web/app.html)
```

`main.c` (abridged, see [`examples/minimal`](examples/minimal/main/main.c)):

```c
void app_main(void)
{
    hi_health_early_boot(3);                      // confirm image, count unverified boots, maybe roll back
    my_safety_critical_init();                    // before anything that can block

    hi_log_init(&(hi_log_config_t) {.udp_ip = "192.168.1.10", .udp_port = 1337, .on_error_line = hi_notify_error});
    hi_wifi_start(&(hi_wifi_config_t) {.ssid = WIFI_SSID, .password = WIFI_PASS, .hostname = "my-device"});
    hi_notify_init(&(hi_notify_config_t) {.topic = NTFY_TOPIC, .error_topic = NTFY_ERROR_TOPIC});
    hi_ntp_start(&(hi_ntp_config_t) {0});
    hi_ota_init(&(hi_ota_config_t) {.url = OTA_URL, .cert_pem = ota_cert, .delete_after = true});

    hi_auth_init(&auth_config);
    hi_httpd_start(&httpd_config);                // + hi_httpd_register() for app routes
    hi_health_start(&(hi_health_config_t) {.is_healthy = my_health_predicate});
    hi_ota_start_background();
}
```

An application handler:

```c
static esp_err_t valve_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, NULL, &result)) return result;
    cJSON *body = hi_httpd_read_json(req, &result);
    if (body == NULL) return result;
    /* ... */
    cJSON_Delete(body);
    return hi_httpd_send_json(req, "200 OK", cJSON_CreateObject());
}
```

## HTTP contract

| Method | Path | Guard | |
|---|---|---|---|
| GET | `/` | host | login page without a session, app page with one (gzip, CSP, `X-Frame-Options: DENY`) |
| GET | `/apple-touch-icon.png`, `/manifest.webmanifest` | host | home-screen app (optional) |
| GET | `/api/session` | host | `{authenticated, csrf, version}` |
| POST | `/api/login` | host + origin | `{password}` → `sid` cookie (HttpOnly, SameSite=Strict) + `{csrf}`; 401/429 with `retry_after_ms` |
| POST | `/api/logout`, `/api/reboot`, `/api/ota` | mutation | |
| POST | `/admin/su`, `/admin/reboot` | `Authorization` header | for scripts |
| GET | `/admin/hw-status` | none | `{up_since, free_mem_kb, version, reset_reason}` |

Mutation = session cookie + `Content-Type: application/json` + `X-CSRF-Token` + `Origin` (when sent) equal to `Host`.

## Tools

| Script | Purpose |
|---|---|
| `tools/hash_password.py` | Prompts for a password, prints the `AUTH_PASSWORD_*` defines |
| `tools/gzip_asset.py` | Deterministic gzip (used by `home_idf_embed_gzip`) |
| `tools/make_icon.py` | 180×180 PNG icon for iOS "Add to Home Screen" |

## Build the example

```sh
./build.sh                      # ESP-IDF 5.4.2, examples/minimal
```

CI builds the example with and without the GCP module.

## Used by

- [water-controller](https://github.com/tomekceszke/water-controller): anti-flood valve with flow metering

## License

MIT
