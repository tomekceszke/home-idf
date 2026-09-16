# Changelog

## 0.1.8 (2026-09-16)

- Shared app shell: `web/app_shell.css` + `web/app_shell.js` + `tools/render_page.py` +
  `home_idf_app_page(<lib> SRC <app.html> NAME <device>)`. The page keeps its own markup and device logic and pulls
  in the shell through placeholders (`@HI_APP_CSS@`, `@HI_APP_JS@`, `@HI_APP_WORDMARK@`, `@HI_APP_TABS@`,
  `@HI_APP_NAME@`); the result is gzipped like `home_idf_embed_gzip()`. One layout for every controller: wordmark and
  status, headline, three numbers, main view, latest events, then a swipe control and small actions docked above the
  tabs Live / History / Settings. The script provides `hi.api` (CSRF, timeouts, 401 reload), `hi.tabs`, `hi.swipe`
  (fires once when the knob reaches the end), `hi.events`, `hi.rows` and duration formatting in words.
- Development servers call `render_page.render()` so they serve the same page as the firmware.

## 0.1.7 (2026-09-16)

- `tools/gzip_asset.py`: HTML is minified before gzipping - comments dropped, `<style>` blocks collapsed, markup
  and scripts untouched (whitespace there can be part of the page). Quoted strings and `url()` keep their spacing.
  Water-controller: sign-in page 1748 -> 1660 B, app 9693 -> 9393 B gzipped, both rendering pixel for pixel the
  same as before.

## 0.1.6 (2026-09-16)

- Shared sign-in page: `web/login.html` + `home_idf_login_page(<lib> NAME <device> [ACCENT <#rrggbb>] [ICONS ON|OFF])`,
  rendered at build time and embedded under the symbols `hi_httpd` already reads, so apps drop their own copy
  without any C change. Left-aligned wordmark split on the name's first hyphen, no device information beyond the
  name, system faces only (the page is served offline). Field and button share one rule, which is what keeps them
  the same height.

## 0.1.5 (2026-09-15)

- `hi_secret` + `tools/obfuscate.py`: obfuscated (not encrypted) credentials, `"obf1:<hex>"`, so Wi-Fi passwords
  and tokens are not readable at a glance in `credentials.h` or with `strings`; plain values keep working.

## 0.1.4 (2026-09-15)

- `hi_ota`: deleting the published image counts as done on any 2xx status (the local OTA server answers without a
  body, which `esp_http_client_perform()` reports as an error). Found in the water-controller migration rehearsal.

## 0.1.3 (2026-09-15)

- `hi_migrator` (optional, `CONFIG_HOME_IDF_MIGRATOR`): OTA migration of a legacy two_ota device to a new
  bootloader and partition table, ported from gate-controller (production-proven); slot offset and safe-state
  handling come from the application.
- `tools/build_migrator.sh`: builds a firmware, embeds its bootloader and table into a migrator project.

## 0.1.2 (2026-09-15)

- `hi_log`: the UDP socket is created by the sender task once WiFi is connected. `hi_log_init()` before
  `hi_wifi_start()` crashed with `tcpip_send_msg_wait_sem (Invalid mbox)`. Found on hardware.

## 0.1.1 (2026-09-15)

- `hi_log`: UDP lines go through a ring buffer and a sender task, so `ESP_LOG` in time-critical tasks never waits
  for the lwIP thread; lines are dropped when the buffer is full.

## 0.1.0 (2026-09-15)

First release, extracted from gate-controller 2.0.2 and water-controller.

- `hi_wifi`: strongest-AP join (all-channel scan, sort by signal, 802.11k/v), reconnect backoff on an
  `esp_timer` instead of sleeping in the system event loop, non-blocking start.
- `hi_log`: serial + UDP log, wall-clock timestamps, optional error-line hook.
- `hi_ntp`: non-blocking SNTP with sync callback and boot time.
- `hi_ota`: HTTPS OTA (blocking or background task), optional DELETE of the published image.
- `hi_notify`: ntfy.sh queue (never blocks senders), priority/tags/click, error de-duplication.
- `hi_health`: brownout-tolerant verification of new images with rollback to the previous slot.
- `hi_auth`: PBKDF2 password, NVS sessions, CSRF HMAC, login back-off, admin header.
- `hi_httpd`: Host/Origin/CSRF guards, JSON helpers, common UI/session/admin routes.
- `hi_gcp` (optional): service account JWT -> Google ID token -> HTTPS POST (from water-controller, leaks fixed).
- `home_idf_embed_gzip()` CMake helper, `tools/gzip_asset.py`, `tools/hash_password.py`, `tools/make_icon.py`.

Migrating an existing device: NVS namespaces are `hi_health` and `hi_auth` (previously `health` and `auth`), so a
migrated image is verified once more and web sessions have to log in again.
