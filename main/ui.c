#include "ui.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "esp_log.h"

static const char *TAG = "UI";

void ui_init(void)
{
    ESP_LOGI(TAG, "Games page opened");
    // Clear to black — no text, no bars, no redraws.
    // Matches the music app's black-background approach to eliminate
    // any DMA artifacts or bleed-through on transitions.
    rg_gui_clear(RG_COLOR_BLACK);
    rg_display_drain();
}

void ui_update(void)
{
    // No-op: no dynamic content to update on the games page.
    // This prevents unnecessary SPI/DMA churn that can cause
    // flicker artifacts on app transitions.
}
