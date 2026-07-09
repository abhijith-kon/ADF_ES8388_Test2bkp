#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "UPDATER";

#define ESP_SD_PIN_CLK  38
#define ESP_SD_PIN_CMD  39
#define ESP_SD_PIN_D0   40
#define ESP_SD_PIN_D1   41
#define ESP_SD_PIN_D2   42
#define ESP_SD_PIN_D3   21

enum OTA_TARGET {
    OTA_CONSOLE = 0,
    OTA_LAUNCHER,
    OTA_RETRO,
    OTA_PRBOOM
};

enum OTA_STATE {
    OTA_IDLE = 0,
    OTA_PENDING,
    OTA_FLASHING,
    OTA_SUCCESS,
    OTA_FAILED
};

static void return_to_console(void) {
    ESP_LOGI(TAG, "Returning to Console OS...");
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory) {
        esp_ota_set_boot_partition(factory);
    }
    esp_restart();
}

static esp_err_t mount_sdcard(void) {
    ESP_LOGI(TAG, "Mounting SD card...");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.clk = ESP_SD_PIN_CLK;
    slot_config.cmd = ESP_SD_PIN_CMD;
    slot_config.d0 = ESP_SD_PIN_D0;
    slot_config.d1 = ESP_SD_PIN_D1;
    slot_config.d2 = ESP_SD_PIN_D2;
    slot_config.d3 = ESP_SD_PIN_D3;

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (0x%x)", ret);
        return ret;
    }
    ESP_LOGI(TAG, "SD card mounted");
    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "OTA Updater Started!");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs;
    err = nvs_open("ota", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No OTA request found");
        return_to_console();
    }

    uint32_t state = OTA_IDLE;
    nvs_get_u32(nvs, "state", &state);

    if (state != OTA_PENDING && state != OTA_FLASHING) {
        ESP_LOGI(TAG, "No pending updates. Exiting.");
        nvs_close(nvs);
        return_to_console();
    }

    // Set state to FLASHING to handle power loss
    state = OTA_FLASHING;
    nvs_set_u32(nvs, "state", state);
    nvs_commit(nvs);

    uint32_t target_app = OTA_CONSOLE;
    nvs_get_u32(nvs, "target", &target_app);

    char file_path[256] = {0};
    size_t path_len = sizeof(file_path);
    nvs_get_str(nvs, "file", file_path, &path_len);

    ESP_LOGI(TAG, "Update Target: %lu", target_app);
    ESP_LOGI(TAG, "Firmware File: %s", file_path);

    if (mount_sdcard() != ESP_OK) {
        ESP_LOGE(TAG, "SD Card missing or failed! Cannot proceed.");
        return_to_console();
    }

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open firmware file: %s", file_path);
        nvs_set_u32(nvs, "state", OTA_FAILED);
        nvs_commit(nvs);
        return_to_console();
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "Firmware size: %zu bytes", file_size);

    const char *part_label = "factory";
    if (target_app == OTA_LAUNCHER) part_label = "launcher";
    else if (target_app == OTA_RETRO) part_label = "retro-core";
    else if (target_app == OTA_PRBOOM) part_label = "prboom-go";

    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, part_label);
    if (!part) {
        ESP_LOGE(TAG, "Partition '%s' not found!", part_label);
        fclose(f);
        nvs_set_u32(nvs, "state", OTA_FAILED);
        nvs_commit(nvs);
        return_to_console();
    }

    if (file_size > part->size) {
        ESP_LOGE(TAG, "Firmware too large for partition! (%zu > %lu)", file_size, part->size);
        fclose(f);
        nvs_set_u32(nvs, "state", OTA_FAILED);
        nvs_commit(nvs);
        return_to_console();
    }

    ESP_LOGI(TAG, "Erasing partition '%s' (size 0x%lx)...", part->label, part->size);
    // Erase enough blocks for the file (must be aligned to 4KB)
    size_t erase_size = (file_size + 4095) & ~4095;
    err = esp_partition_erase_range(part, 0, erase_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed! (0x%x)", err);
        fclose(f);
        nvs_set_u32(nvs, "state", OTA_FAILED);
        nvs_commit(nvs);
        return_to_console();
    }

    ESP_LOGI(TAG, "Flashing...");
    uint8_t *buf = malloc(4096);
    size_t offset = 0;
    while (offset < file_size) {
        size_t read_bytes = fread(buf, 1, 4096, f);
        if (read_bytes == 0) break;

        err = esp_partition_write(part, offset, buf, read_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at offset 0x%zx! (0x%x)", offset, err);
            free(buf);
            fclose(f);
            nvs_set_u32(nvs, "state", OTA_FAILED);
            nvs_commit(nvs);
            return_to_console();
        }
        offset += read_bytes;
        if (offset % (4096 * 32) == 0) { // Log every 128KB
            ESP_LOGI(TAG, "Progress: %zu / %zu bytes", offset, file_size);
        }
    }
    free(buf);
    fclose(f);

    if (offset != file_size) {
        ESP_LOGE(TAG, "File read incomplete!");
        nvs_set_u32(nvs, "state", OTA_FAILED);
        nvs_commit(nvs);
        return_to_console();
    }

    ESP_LOGI(TAG, "Flash successful! Deleting firmware file...");
    unlink(file_path);

    nvs_set_u32(nvs, "state", OTA_SUCCESS);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Update complete! Rebooting into Console OS...");
    return_to_console();
}
