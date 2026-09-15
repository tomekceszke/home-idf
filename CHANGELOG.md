# Changelog

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
