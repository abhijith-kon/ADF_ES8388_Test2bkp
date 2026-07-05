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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    /* ---- Panel init sequence ---- */
    esp_lcd_panel_reset(panel_handle);      // Sends software reset command
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_lcd_panel_init(panel_handle);       // Full ILI9341 init (sleep out, power, gamma, etc.)
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ---- Orientation & display on ---- */
    esp_lcd_panel_disp_on_off(panel_handle, true);
    /* Initialize directly in portrait mode (240x320) to match UI layout.
     * Avoids the landscape→portrait MADCTL transition that can leave
     * edge pixels in an undefined state (right-edge brightness bug). */
    esp_lcd_panel_swap_xy(panel_handle, false);
    esp_lcd_panel_mirror(panel_handle, true, false);    // MX=1 for correct GoldenMorning orientation
    esp_lcd_panel_invert_color(panel_handle, false);    // No inversion for this panel
    esp_lcd_panel_set_gap(panel_handle, 0, 0);          // Explicit zero column/row offset

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
    // With trans_queue_depth = 1 and quad buffering, DMA transfers complete safely.
    // Sending NOP command (0x00) over SPI after every primitive draw caused TFT controller
    // driver glitches resulting in white screen flashes and flickering on redraws.
    // Yielding 200us allows any in-flight SPI DMA bus transaction to finish cleanly
    // without sending bogus commands to the display panel.
    esp_rom_delay_us(200);
}

void rg_display_set_config(rg_display_config_t config)
{
    if (!panel_handle) return;

    switch (config.rotation) {
        case RG_SCREEN_ROTATION_0:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, true, false);
            break;
        case RG_SCREEN_ROTATION_90:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, false, false);
            break;
        case RG_SCREEN_ROTATION_180:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, true, true);
            break;
        case RG_SCREEN_ROTATION_270:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, true, false);
            break;
    }
}

void rg_display_set_backlight(float level)
{
    // Backlight is hardwired to 3.3V on this board — no software control.
    // Future: PWM backlight via LEDC if wired to a GPIO.
    ESP_LOGD(TAG, "Backlight set request (%.0f%%) — hardwired, no effect.", level * 100);
}
