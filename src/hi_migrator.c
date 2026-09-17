/* See hi_migrator.h. Ported from gate-controller's migrator (production-proven on 2026-09-15). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

#include "hi_migrator.h"
#include "hi_ota.h"
#include "hi_system.h"

static const char *TAG = "HI_MIGRATOR";

#define BOOTLOADER_OFFSET   0x1000
#define BOOTLOADER_REGION   0x7000      // up to the partition table
#define PT_OFFSET           CONFIG_PARTITION_TABLE_OFFSET
#define PT_REGION           0x1000      // one sector; nvs starts at 0x9000
#define INSTALL_RETRY_S     60

typedef enum {
    STAGE_BOOT,
    STAGE_RELOCATING,
    STAGE_READY,
    STAGE_NOT_READY,        // checks failed: nothing will be written
    STAGE_COMMITTING,
    STAGE_INSTALLING,
    STAGE_WAITING_MANUAL,   // the firmware in ota_0 was rolled back once: /admin/su to retry
    STAGE_ERROR,
} stage_t;

static const char *STAGE_NAMES[] = {
    "boot", "relocating", "ready", "not_ready", "committing", "installing", "waiting_manual", "error",
};

static hi_migrator_config_t s_cfg;
static volatile stage_t s_stage = STAGE_BOOT;
static bool s_table_new;
static bool s_bootloader_new;
static uint32_t s_running_offset;
static uint32_t s_running_len;
static char s_report[1200];
static char s_error[160];

/* ---------- helpers ---------- */

static void report(const char *fmt, ...)
{
    size_t used = strlen(s_report);
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_report + used, sizeof(s_report) - used, fmt, args);
    va_end(args);
    ESP_LOGW(TAG, "check: %s", s_report + used);
    used = strlen(s_report);
    if (used + 1 < sizeof(s_report)) {
        s_report[used] = '\n';
        s_report[used + 1] = '\0';
    }
}

static void fail(const char *msg)
{
    snprintf(s_error, sizeof(s_error), "%s", msg);
    ESP_LOGE(TAG, "%s", msg);
}

static bool flash_equals(uint32_t offset, const uint8_t *data, size_t len)
{
    uint8_t buf[512];
    for (size_t pos = 0; pos < len; pos += sizeof(buf)) {
        size_t n = len - pos < sizeof(buf) ? len - pos : sizeof(buf);
        if (esp_flash_read(NULL, buf, offset + pos, n) != ESP_OK || memcmp(buf, data + pos, n) != 0) {
            return false;
        }
    }
    return true;
}

static bool sha256_equals(const uint8_t *data, size_t len, const uint8_t *expected)
{
    uint8_t digest[32];
    mbedtls_sha256(data, len, digest, 0);
    return memcmp(digest, expected, sizeof(digest)) == 0;
}

static bool admin_header_valid(httpd_req_t *req)
{
    size_t expected_len = s_cfg.admin_header_value ? strlen(s_cfg.admin_header_value) : 0;
    char value[128];
    if (expected_len == 0 || expected_len >= sizeof(value)
        || httpd_req_get_hdr_value_len(req, "Authorization") != expected_len
        || httpd_req_get_hdr_value_str(req, "Authorization", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < expected_len; i++) {
        diff |= (uint8_t) (value[i] ^ s_cfg.admin_header_value[i]);
    }
    return diff == 0;
}

/* ---------- checks ---------- */

static const esp_partition_info_t *find_entry(const esp_partition_info_t *table, int count, uint8_t type,
                                              uint8_t subtype)
{
    for (int i = 0; i < count; i++) {
        if (table[i].magic == ESP_PARTITION_MAGIC && table[i].type == type && table[i].subtype == subtype) {
            return &table[i];
        }
    }
    return NULL;
}

static bool current_partition_at(esp_partition_type_t type, esp_partition_subtype_t subtype, uint32_t offset,
                                 uint32_t size)
{
    const esp_partition_t *p = esp_partition_find_first(type, subtype, NULL);
    return p != NULL && p->address == offset && p->size == size;
}

/* Everything that must hold before a single byte of the bootloader or table is written. */
static bool run_checks(void)
{
    bool ok = true;
    s_report[0] = '\0';

    const uint8_t *bl = s_cfg.bootloader;
    size_t bl_len = s_cfg.bootloader_len;
    bool bl_blob_ok = bl_len > 4 && bl_len <= BOOTLOADER_REGION && sha256_equals(bl, bl_len, s_cfg.bootloader_sha256)
                      && bl[0] == ESP_IMAGE_HEADER_MAGIC;
    report("bootloader blob %u B, sha256 + image magic: %s", (unsigned) bl_len, bl_blob_ok ? "ok" : "FAIL");
    ok &= bl_blob_ok;

    // Flash mode, size and speed of the new bootloader must match the one in flash
    uint8_t cur_hdr[4];
    bool hdr_ok = esp_flash_read(NULL, cur_hdr, BOOTLOADER_OFFSET, sizeof(cur_hdr)) == ESP_OK
                  && cur_hdr[0] == ESP_IMAGE_HEADER_MAGIC && bl_len > 4 && cur_hdr[2] == bl[2] && cur_hdr[3] == bl[3];
    report("bootloader in flash has the same flash settings (mode 0x%02x, size/speed 0x%02x): %s",
           cur_hdr[2], cur_hdr[3], hdr_ok ? "ok" : "FAIL");
    ok &= hdr_ok;

    const esp_partition_info_t *table = (const esp_partition_info_t *) s_cfg.partition_table;
    int count = 0;
    bool pt_ok = s_cfg.partition_table_len >= ESP_PARTITION_TABLE_MAX_LEN
                 && sha256_equals(s_cfg.partition_table, s_cfg.partition_table_len, s_cfg.partition_table_sha256)
                 && esp_partition_table_verify(table, true, &count) == ESP_OK;
    report("partition table blob sha256 + MD5 verify (%d entries): %s", count, pt_ok ? "ok" : "FAIL");
    ok &= pt_ok;

    if (pt_ok) {
        const esp_partition_info_t *nvs = find_entry(table, count, PART_TYPE_DATA, PART_SUBTYPE_DATA_WIFI);
        const esp_partition_info_t *otadata = find_entry(table, count, PART_TYPE_DATA, PART_SUBTYPE_DATA_OTA);
        const esp_partition_info_t *phy = find_entry(table, count, PART_TYPE_DATA, PART_SUBTYPE_DATA_RF);
        const esp_partition_info_t *ota0 = find_entry(table, count, PART_TYPE_APP, PART_SUBTYPE_OTA_FLAG | 0);
        const esp_partition_info_t *ota1 = find_entry(table, count, PART_TYPE_APP, PART_SUBTYPE_OTA_FLAG | 1);
        bool same_data = nvs && otadata && phy
            && current_partition_at(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nvs->pos.offset, nvs->pos.size)
            && current_partition_at(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, otadata->pos.offset,
                                    otadata->pos.size)
            && current_partition_at(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, phy->pos.offset, phy->pos.size);
        report("nvs/otadata/phy_init identical in old and new table: %s", same_data ? "ok" : "FAIL");
        ok &= same_data;

        bool slots = ota0 && ota1 && ota1->pos.offset == s_running_offset && ota1->pos.size >= s_running_len
                     && ota0->pos.offset + ota0->pos.size <= ota1->pos.offset;
        report("new ota_1 starts at this image (0x%06lx, %lu B fits): %s",
               (unsigned long) s_running_offset, (unsigned long) s_running_len, slots ? "ok" : "FAIL");
        ok &= slots;

        uint32_t flash_size = 0;
        esp_flash_get_size(NULL, &flash_size);
        bool fits = true;
        for (int i = 0; i < count; i++) {
            if (table[i].magic == ESP_PARTITION_MAGIC && table[i].pos.offset + table[i].pos.size > flash_size) {
                fits = false;
            }
        }
        report("all partitions inside %lu B flash: %s", (unsigned long) flash_size, fits ? "ok" : "FAIL");
        ok &= fits;
    }

    bool power_ok = esp_reset_reason() != ESP_RST_BROWNOUT;
    report("last reset not a brownout: %s", power_ok ? "ok" : "FAIL (reboot on stable power first)");
    ok &= power_ok;

    char bootloader_idf[33];
    report("bootloader in flash built with ESP-IDF %s",
           hi_bootloader_idf(bootloader_idf, sizeof(bootloader_idf)) ? bootloader_idf : "unknown (no description, pre-5.1)");

    report("already new: partition table %s, bootloader %s", s_table_new ? "yes" : "no", s_bootloader_new ? "yes" : "no");
    return ok;
}

/* ---------- stages ---------- */

static void relocate_to_ota1(const esp_partition_t *running)
{
    s_stage = STAGE_RELOCATING;
    const esp_partition_t *target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (target == NULL || target->address != s_cfg.ota1_offset || target->size < s_running_len) {
        fail("relocate: legacy ota_1 is not at the configured offset or too small");
        s_stage = STAGE_ERROR;
        return;
    }
    ESP_LOGW(TAG, "Copying this image (%lu B) from 0x%06lx to ota_1 @ 0x%06lx",
             (unsigned long) s_running_len, (unsigned long) running->address, (unsigned long) target->address);
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, s_running_len, &handle);
    uint8_t *buf = malloc(4096);
    if (buf == NULL) err = ESP_ERR_NO_MEM;
    for (uint32_t pos = 0; err == ESP_OK && pos < s_running_len; pos += 4096) {
        uint32_t n = s_running_len - pos < 4096 ? s_running_len - pos : 4096;
        err = esp_partition_read(running, pos, buf, n);
        if (err == ESP_OK) err = esp_ota_write(handle, buf, n);
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(handle);       // verifies the copied image
    if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "relocate failed: %s", esp_err_to_name(err));
        fail(msg);
        s_stage = STAGE_ERROR;
        return;
    }
    ESP_LOGW(TAG, "Relocated, restarting into ota_1");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static bool write_region(uint32_t offset, uint32_t region_len, const uint8_t *data, size_t len, const char *what)
{
    uint8_t *copy = malloc(len);
    if (copy == NULL) return false;
    memcpy(copy, data, len);    // never write straight from flash-mapped rodata
    for (int attempt = 1; attempt <= 3; attempt++) {
        int64_t t0 = esp_timer_get_time();
        esp_err_t err = esp_flash_erase_region(NULL, offset, region_len);
        if (err == ESP_OK) err = esp_flash_write(NULL, copy, offset, len);
        bool verified = err == ESP_OK && flash_equals(offset, copy, len);
        ESP_LOGW(TAG, "%s @0x%05lx attempt %d: %s, verify %s (%lld ms)", what, (unsigned long) offset, attempt,
                 esp_err_to_name(err), verified ? "ok" : "FAILED", (esp_timer_get_time() - t0) / 1000);
        if (verified) {
            free(copy);
            return true;
        }
    }
    free(copy);
    return false;
}

static void commit_task(void *arg)
{
    ESP_LOGW(TAG, "COMMIT in 2 s: partition table first, then bootloader. Do not cut power.");
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint8_t *old_table = malloc(ESP_PARTITION_TABLE_MAX_LEN);
    uint8_t *old_bootloader = malloc(BOOTLOADER_REGION);
    if (old_table == NULL || old_bootloader == NULL
        || esp_flash_read(NULL, old_table, PT_OFFSET, ESP_PARTITION_TABLE_MAX_LEN) != ESP_OK
        || esp_flash_read(NULL, old_bootloader, BOOTLOADER_OFFSET, BOOTLOADER_REGION) != ESP_OK) {
        fail("commit: could not back up the current regions, nothing written");
        s_stage = STAGE_ERROR;
        goto out;
    }
    if (!s_table_new) {
        if (!write_region(PT_OFFSET, PT_REGION, s_cfg.partition_table, s_cfg.partition_table_len, "partition table")) {
            bool restored = write_region(PT_OFFSET, PT_REGION, old_table, ESP_PARTITION_TABLE_MAX_LEN, "restore partition table");
            fail(restored ? "commit: partition table write failed, old table restored"
                          : "commit: partition table write AND restore failed: do not reboot, needs USB");
            s_stage = STAGE_ERROR;
            goto out;
        }
        s_table_new = true;
    }
    if (!s_bootloader_new) {
        if (!write_region(BOOTLOADER_OFFSET, BOOTLOADER_REGION, s_cfg.bootloader, s_cfg.bootloader_len, "bootloader")) {
            bool restored = write_region(BOOTLOADER_OFFSET, BOOTLOADER_REGION, old_bootloader, BOOTLOADER_REGION,
                                         "restore bootloader");
            fail(restored ? "commit: bootloader write failed, old bootloader restored (new table stays, bootable)"
                          : "commit: bootloader write AND restore failed: do not reboot, needs USB");
            s_stage = STAGE_ERROR;
            goto out;
        }
        s_bootloader_new = true;
    }
    ESP_LOGW(TAG, "COMMIT done: new partition table and bootloader verified. Restarting.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

out:
    free(old_table);
    free(old_bootloader);
    vTaskDelete(NULL);
}

static void install_task(void *arg)
{
    const esp_partition_t *ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_ota_img_states_t state;
    if (ota0 != NULL && esp_ota_get_state_partition(ota0, &state) == ESP_OK
        && (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)) {
        ESP_LOGE(TAG, "Firmware in ota_0 was rolled back: not reinstalling automatically (POST /admin/su to retry)");
        s_stage = STAGE_WAITING_MANUAL;
        vTaskDelete(NULL);
    }
    for (;;) {
        ESP_LOGW(TAG, "Installing firmware into ota_0");
        hi_ota_run();       // restarts on success
        ESP_LOGW(TAG, "Firmware not available yet, retry in %d s", INSTALL_RETRY_S);
        vTaskDelay(pdMS_TO_TICKS(INSTALL_RETRY_S * 1000));
    }
}

/* ---------- HTTP ---------- */

static esp_err_t status_handler(httpd_req_t *req)
{
    char *body = malloc(2000);
    if (body == NULL) return httpd_resp_send_500(req);
    char escaped[1300];
    size_t j = 0;
    for (size_t i = 0; s_report[i] && j + 2 < sizeof(escaped); i++) {
        if (s_report[i] == '\n') {
            escaped[j++] = '\\';
            escaped[j++] = 'n';
        } else if (s_report[i] != '"' && s_report[i] != '\\') {
            escaped[j++] = s_report[i];
        }
    }
    escaped[j] = '\0';
    snprintf(body, 2000,
             "{\"migrator\":\"%s\",\"version\":\"%s\",\"stage\":\"%s\",\"running_offset\":\"0x%06lx\","
             "\"table_new\":%s,\"bootloader_new\":%s,\"error\":\"%s\",\"checks\":\"%s\"}",
             s_cfg.name, esp_app_get_description()->version, STAGE_NAMES[s_stage], (unsigned long) s_running_offset,
             s_table_new ? "true" : "false", s_bootloader_new ? "true" : "false", s_error, escaped);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static esp_err_t deny(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, msg);
}

static esp_err_t commit_handler(httpd_req_t *req)
{
    if (!admin_header_valid(req)) return deny(req, "401 Unauthorized", "");
    if (s_stage != STAGE_READY) return deny(req, "409 Conflict", "not in stage ready");
    s_stage = STAGE_COMMITTING;     // reserve before the task starts: a second request gets 409
    if (xTaskCreate(commit_task, "commit", 6144, NULL, 10, NULL) != pdPASS) {
        s_stage = STAGE_READY;
        return deny(req, "500 Internal Server Error", "task");
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, "Commit started, watch the UDP log");
}

static esp_err_t su_handler(httpd_req_t *req)
{
    if (!admin_header_valid(req)) return deny(req, "401 Unauthorized", "");
    // Only once the new table is in place: an OTA with the old table would target a legacy slot
    if (s_stage != STAGE_INSTALLING && s_stage != STAGE_WAITING_MANUAL) return deny(req, "409 Conflict", "not installing");
    bool started = hi_ota_start_background();
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, started ? "Upgrade in progress..." : "Upgrade already running");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    if (!admin_header_valid(req)) return deny(req, "401 Unauthorized", "");
    if (s_stage == STAGE_COMMITTING) return deny(req, "409 Conflict", "commit in progress");
    hi_restart_soon(500);
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, "Reboot in progress...");
}

static void start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = s_cfg.http_port ? s_cfg.http_port : 80;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed");
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/migrator", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/admin/hw-status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/migrator/commit", .method = HTTP_POST, .handler = commit_handler},
        {.uri = "/admin/su", .method = HTTP_POST, .handler = su_handler},
        {.uri = "/admin/reboot", .method = HTTP_POST, .handler = reboot_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
}

/* ---------- main ---------- */

void hi_migrator_run(const hi_migrator_config_t *config)
{
    s_cfg = *config;
    // Under the new rollback bootloader an unconfirmed image would be rolled back on the next reset
    esp_ota_mark_app_valid_cancel_rollback();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_partition_pos_t pos = {.offset = running->address, .size = running->size};
    esp_image_metadata_t meta;
    s_running_offset = running->address;
    s_running_len = esp_image_get_metadata(&pos, &meta) == ESP_OK ? meta.image_len : running->size;
    s_table_new = flash_equals(PT_OFFSET, s_cfg.partition_table, s_cfg.partition_table_len);
    s_bootloader_new = flash_equals(BOOTLOADER_OFFSET, s_cfg.bootloader, s_cfg.bootloader_len);

    ESP_LOGW(TAG, "%s %s: running %s @0x%06lx (%lu B), reset %s, table %s, bootloader %s", s_cfg.name,
             esp_app_get_description()->version, running->label, (unsigned long) running->address,
             (unsigned long) s_running_len, hi_reset_reason(), s_table_new ? "NEW" : "old",
             s_bootloader_new ? "NEW" : "old");
    start_http();

    if (s_table_new && s_bootloader_new) {
        if (running->address == s_cfg.ota1_offset) {
            s_stage = STAGE_INSTALLING;
            if (xTaskCreate(install_task, "install", 8192, NULL, 4, NULL) != pdPASS) {
                fail("install task not created");
                s_stage = STAGE_ERROR;
            }
        } else {
            fail("new layout but not running from ota_1: nothing to do");
            s_stage = STAGE_ERROR;
        }
        return;
    }
    if (running->address != s_cfg.ota1_offset) {
        if (s_table_new) {
            fail("table already new but running outside ota_1: refusing to touch anything");
            s_stage = STAGE_ERROR;
            return;
        }
        relocate_to_ota1(running);
        return;
    }
    s_stage = run_checks() ? STAGE_READY : STAGE_NOT_READY;
    ESP_LOGW(TAG, "Stage: %s. %s", STAGE_NAMES[s_stage],
             s_stage == STAGE_READY ? "POST /migrator/commit with the admin Authorization header to proceed."
                                    : "Fix the failed checks; nothing will be written.");
}
