#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "board.h"
#include "soc/soc_caps.h"

static const char *TAG = "APP_OTG";

extern void system_sdcard_suspend(void);
extern void system_sdcard_resume(void);

static sdmmc_card_t *s_card = NULL;
static sdmmc_host_t s_host = SDMMC_HOST_DEFAULT();
static bool otg_active = false;

static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    ESP_LOGI(TAG, "Storage mounted to host: %s", event->mount_changed_data.is_mounted ? "Yes" : "No");
}

void app_otg_start(void)
{
    if (otg_active) return;
    ESP_LOGI(TAG, "Entering OTG Mode. Suspending system SD card...");
    system_sdcard_suspend(); // This destroys the VFS and deinitializes the SDMMC driver

    // Re-initialize SDMMC for TinyUSB MSC
    s_host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

#if SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = ESP_SD_PIN_CLK;
    slot_config.cmd = ESP_SD_PIN_CMD;
    slot_config.d0 = ESP_SD_PIN_D0;
    slot_config.d1 = ESP_SD_PIN_D1;
    slot_config.d2 = ESP_SD_PIN_D2;
    slot_config.d3 = ESP_SD_PIN_D3;
#endif

    s_card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (!s_card) {
        ESP_LOGE(TAG, "Failed to allocate sdmmc_card_t");
        return;
    }

    (*s_host.init)();
    sdmmc_host_init_slot(s_host.slot, &slot_config);

    if (sdmmc_card_init(&s_host, s_card) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SD card for OTG");
        free(s_card);
        s_card = NULL;
        return;
    }

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = s_card,
        .callback_mount_changed = storage_mount_changed_cb,
        .mount_config.max_files = 5,
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_sdmmc(&config_sdmmc));

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
        .configuration_descriptor = NULL, // Use default TinyUSB descriptors
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    otg_active = true;
    ESP_LOGI(TAG, "OTG Mode Active");
}

void app_otg_stop(void)
{
    if (!otg_active) return;
    ESP_LOGI(TAG, "Stopping OTG Mode...");

    tinyusb_msc_storage_deinit();
    tinyusb_driver_uninstall();

    if (s_card) {
        free(s_card);
        s_card = NULL;
    }
    (*s_host.deinit)();

    ESP_LOGI(TAG, "Resuming system SD card...");
    system_sdcard_resume();
    otg_active = false;
}

int app_otg_get_progress(void)
{
    if (!otg_active) return 0;
    return tinyusb_msc_storage_in_use_by_usb_host() ? 1 : 0;
}
