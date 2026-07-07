#include "rg_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"
#include "board_pins_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

/*
 * Display Driver — GoldenMorning 2.8" TFT LCD (ILI9341)
 *
 * Panel: GoldenMorning 2.8" TFT Negative Transmissive, Non-Touch
 * Controller: ILI9341
 * Resolution: 240×320 native (used in 320×240 landscape via swap_xy)
 * Interface: SPI (CS=10, DC=9, RST=14, MOSI=13, SCK=12)
 * Backlight: Hardwired to 3.3V (always on)
 * Color: 16-bit RGB565, BGR element order (ILI9341 native)
 */

static const char *TAG = "rg_display";
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

#define LCD_H_RES   240
#define LCD_V_RES   320
/* Landscape dimensions after swap_xy */
#define LCD_LAND_W  320
#define LCD_LAND_H  240

/* Diagnostic contrast & gamma profiles for ILI9341-compatible panels:
 * 0 = Default ESP-IDF vendor init (washed out on some new clones)
 * 1 = Standard Adafruit / ILI9341 specification gamma (tested in Step 2: still low contrast)
 * 2 = High-Contrast Clone profile: Adjusted GVDD (0xC0), VCOM (0xC5/0xC7), and IPS/Clone E0/E1 gamma
 * 3 = Ultra-High Contrast profile: GVDD=0x28, VCOM={0x3E,0x28}, linear E0/E1 gamma
 */
#define CONFIG_ILI9341_CONTRAST_PROFILE  2

/* Display Orientation / Mirroring configuration (with swap_xy=true):
 * Mode 4: mirror_x=false, mirror_y=false (horizontally mirrored on some panels)
 * Mode 5: mirror_x=true,  mirror_y=false
 * Mode 6: mirror_x=false, mirror_y=true (un-mirrors horizontal axis when MY controls left/right scan)
 * Mode 7: mirror_x=true,  mirror_y=true
 */
#define CONFIG_ILI9341_MIRROR_X  false
#define CONFIG_ILI9341_MIRROR_Y  true

void rg_display_init(void)
{
    ESP_LOGI(TAG, "Initializing GoldenMorning 2.8\" ILI9341...");

    /* ---- Hard reset via RST pin ---- */
    gpio_set_direction(SPI_LCD_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SPI_LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(SPI_LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* ---- SPI bus ---- */
    ESP_LOGI(TAG, "Initializing SPI bus at 16MHz...");
    spi_bus_config_t buscfg = {
        .sclk_io_num = SPI_LCD_SCK_PIN,
        .mosi_io_num = SPI_LCD_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_LAND_W * LCD_LAND_H * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* ---- Panel IO (SPI device) ---- */
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = SPI_LCD_DC_PIN,
        .cs_gpio_num = SPI_LCD_CS_PIN,
        .pclk_hz = 16 * 1000 * 1000,   // 16MHz (up from 4MHz safe mode)
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,         // Force blocking SPI: each DMA completes before returning
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    /* ---- ILI9341 panel driver ---- */
    ESP_LOGI(TAG, "Installing ILI9341 panel driver...");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,   // Already reset manually above
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    /* ---- Panel init sequence ---- */
    esp_lcd_panel_reset(panel_handle);      // Sends software reset command
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_lcd_panel_init(panel_handle);       // Full ILI9341 init (sleep out, power, gamma, etc.)
    vTaskDelay(pdMS_TO_TICKS(100));

#if CONFIG_ILI9341_CONTRAST_PROFILE == 1
    /* Profile 1: Standard Adafruit / ILI9341 specification gamma */
    esp_lcd_panel_io_tx_param(io_handle, 0x26, (uint8_t[]){0x01}, 1);
    const uint8_t pos_gamma[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    const uint8_t neg_gamma[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
    esp_lcd_panel_io_tx_param(io_handle, 0xE0, pos_gamma, sizeof(pos_gamma));
    esp_lcd_panel_io_tx_param(io_handle, 0xE1, neg_gamma, sizeof(neg_gamma));
#elif CONFIG_ILI9341_CONTRAST_PROFILE == 2
    /* Profile 2: High-Contrast Clone Profile (GoldenMorning / GC9306 / ILI9341V compatibility)
     * Raises GVDD and calibrates VCOM common voltages to eliminate milky gray blacks. */
    esp_lcd_panel_io_tx_param(io_handle, 0xC0, (uint8_t[]){0x26}, 1);               // Power Control 1: GVDD = 4.95V
    esp_lcd_panel_io_tx_param(io_handle, 0xC1, (uint8_t[]){0x11}, 1);               // Power Control 2
    esp_lcd_panel_io_tx_param(io_handle, 0xC5, (uint8_t[]){0x35, 0x3E}, 2);         // VCOM Control 1: VCOMH/VCOML
    esp_lcd_panel_io_tx_param(io_handle, 0xC7, (uint8_t[]){0xBE}, 1);               // VCOM Control 2
    esp_lcd_panel_io_tx_param(io_handle, 0x26, (uint8_t[]){0x01}, 1);               // Gamma Curve 1
    const uint8_t pos_gamma[] = {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F};
    const uint8_t neg_gamma[] = {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F};
    esp_lcd_panel_io_tx_param(io_handle, 0xE0, pos_gamma, sizeof(pos_gamma));
    esp_lcd_panel_io_tx_param(io_handle, 0xE1, neg_gamma, sizeof(neg_gamma));
#elif CONFIG_ILI9341_CONTRAST_PROFILE == 3
    /* Profile 3: Ultra-High Contrast Profile (Max GVDD + Adafruit VCOM + Standard E0/E1) */
    esp_lcd_panel_io_tx_param(io_handle, 0xC0, (uint8_t[]){0x28}, 1);               // Power Control 1: GVDD = 5.05V
    esp_lcd_panel_io_tx_param(io_handle, 0xC1, (uint8_t[]){0x10}, 1);               // Power Control 2
    esp_lcd_panel_io_tx_param(io_handle, 0xC5, (uint8_t[]){0x3E, 0x28}, 2);         // VCOM Control 1
    esp_lcd_panel_io_tx_param(io_handle, 0xC7, (uint8_t[]){0x86}, 1);               // VCOM Control 2
    esp_lcd_panel_io_tx_param(io_handle, 0x26, (uint8_t[]){0x01}, 1);               // Gamma Curve 1
    const uint8_t pos_gamma[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    const uint8_t neg_gamma[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
    esp_lcd_panel_io_tx_param(io_handle, 0xE0, pos_gamma, sizeof(pos_gamma));
    esp_lcd_panel_io_tx_param(io_handle, 0xE1, neg_gamma, sizeof(neg_gamma));
#endif

    /* Explicitly verify Normal Display Mode (0x13) and Display ON (0x29) */
    esp_lcd_panel_io_tx_param(io_handle, 0x13, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(io_handle, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ---- Orientation & display on ---- */
    esp_lcd_panel_disp_on_off(panel_handle, true);
    /* Initialize directly in portrait mode (240x320) to match UI layout.
     * Mode 4 configuration (swap_xy=1, MX=0, MY=0) determined experimentally
     * for the new GoldenMorning/ILI9341-compatible panel revision. */
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, CONFIG_ILI9341_MIRROR_X, CONFIG_ILI9341_MIRROR_Y);   // Mode 6 un-mirrors horizontally
    esp_lcd_panel_invert_color(panel_handle, false);    // No inversion for this panel
    esp_lcd_panel_set_gap(panel_handle, 0, 0);          // Explicit zero column/row offset
    ESP_LOGI(TAG, "Panel init complete: ContrastProfile=%d, MirrorX=%d, MirrorY=%d",
             CONFIG_ILI9341_CONTRAST_PROFILE, CONFIG_ILI9341_MIRROR_X, CONFIG_ILI9341_MIRROR_Y);

    /* ---- Clear screen to black (portrait: 240x320) ---- */
    uint16_t *buf = malloc(LCD_H_RES * 2);
    if (buf) {
        memset(buf, 0x00, LCD_H_RES * 2);              // Black (0x0000)
        for (int y = 0; y < LCD_V_RES; y++) {
            esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + 1, buf);
        }
        free(buf);
        ESP_LOGI(TAG, "Display cleared (240x320 portrait, 16MHz SPI).");
    }
}

void rg_display_write(int x, int y, int width, int height, int stride, const void *buffer)
{
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + width, y + height, buffer);
    }
}

void rg_display_drain(void)
{
    // Calling tx_param with cmd -1 blocks until all queued DMA transfers finish,
    // without transmitting any command or NOP byte over SPI to the ILI9341 panel.
    if (io_handle) {
        esp_lcd_panel_io_tx_param(io_handle, -1, NULL, 0);
    }
}

void rg_display_set_config(rg_display_config_t config)
{
    if (!panel_handle) return;

    switch (config.rotation) {
        case RG_SCREEN_ROTATION_0:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, CONFIG_ILI9341_MIRROR_X, CONFIG_ILI9341_MIRROR_Y);
            break;
        case RG_SCREEN_ROTATION_90:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, !CONFIG_ILI9341_MIRROR_X, !CONFIG_ILI9341_MIRROR_Y);
            break;
        case RG_SCREEN_ROTATION_180:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, !CONFIG_ILI9341_MIRROR_X, !CONFIG_ILI9341_MIRROR_Y);
            break;
        case RG_SCREEN_ROTATION_270:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, CONFIG_ILI9341_MIRROR_X, CONFIG_ILI9341_MIRROR_Y);
            break;
    }
}

void rg_display_set_backlight(float level)
{
    // Backlight is hardwired to 3.3V on this board — no software control.
    // Future: PWM backlight via LEDC if wired to a GPIO.
    ESP_LOGD(TAG, "Backlight set request (%.0f%%) — hardwired, no effect.", level * 100);
}
