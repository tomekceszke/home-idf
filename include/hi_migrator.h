#pragma once

/*
 * Optional (CONFIG_HOME_IDF_MIGRATOR): one-shot OTA image that moves a legacy device (old bootloader,
 * partitions_two_ota) onto a new bootloader and partition table over the air, then installs the real firmware.
 *
 * Stages are derived from the flash contents on every boot (idempotent):
 *   RELOCATE  running outside ota1_offset with the old table -> copy this image to the legacy ota_1, boot it
 *   READY     running at ota1_offset, old table/bootloader    -> run checks, wait for POST /migrator/commit
 *   COMMIT    write partition table, then bootloader (verify, retry, restore on failure), restart
 *   INSTALL   new table + bootloader, running at ota1_offset  -> OTA the firmware into ota_0
 * Only an interrupted erase/write inside COMMIT can leave the board unbootable (well under a second).
 *
 * Requirements: the new table keeps nvs/otadata/phy_init offsets and places its ota_1 at the legacy ota_1 offset,
 * so every intermediate state stays bootable. The migrator project must be built with the legacy table layout
 * (image fits a legacy slot), CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE and CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;                       // e.g. "water-migrator", for logs and GET /migrator
    const uint8_t *bootloader;              // target bootloader.bin (embedded)
    size_t bootloader_len;
    const uint8_t *bootloader_sha256;       // 32 bytes, from the build script
    const uint8_t *partition_table;         // target partition-table.bin (embedded)
    size_t partition_table_len;
    const uint8_t *partition_table_sha256;  // 32 bytes
    uint32_t ota1_offset;                   // legacy ota_1 offset == new ota_1 offset (0x210000 for two_ota)
    const char *admin_header_value;         // Authorization header for commit/su/reboot
    uint16_t http_port;                     // 0 = 80
} hi_migrator_config_t;

/* Call from app_main after the application made its outputs safe, hi_wifi_start() and hi_ota_init()
 * (the OTA URL is where the real firmware will be published). Returns after starting the stage machine;
 * the HTTP server keeps running: GET /migrator, GET /admin/hw-status, POST /migrator/commit, POST /admin/su,
 * POST /admin/reboot. */
void hi_migrator_run(const hi_migrator_config_t *config);
