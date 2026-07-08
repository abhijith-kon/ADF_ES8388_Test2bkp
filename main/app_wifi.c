#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_wifi.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <sys/param.h>
#include <sys/time.h>
#include <ctype.h>

static void urldecode2(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static const char *TAG = "APP_WIFI";

#define SCREEN_W 240
#define SCREEN_H 320
#define APP_BG RG_COLOR_BLACK

static int selected_option = 0;
static const char *options[] = {
    "WAP",
    "OTG",
    "OTA"
};
#define NUM_OPTIONS 3

static int current_view = 0; // 0 = Menu, 1 = WAP
static bool ui_dirty = false;

// WAP State
static bool wifi_initialized = false;
static esp_netif_t *ap_netif = NULL;
static httpd_handle_t server = NULL;
static int wap_progress = -1; // -1 = waiting, 0-100 = progress
static char wap_status_text[256] = "Waiting for connection...";
static char wap_ip_str[64] = "IP: 192.168.4.1";

static const char *index_html = 
"<!DOCTYPE html><html><head><title>Retro Console Transfer</title>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body { font-family: Arial; background: #222; color: #fff; text-align: center; padding: 50px; }"
"button, input { padding: 10px; margin: 10px; font-size: 16px; border-radius: 8px; border: none; }"
"button { background: #5c20d0; color: white; cursor: pointer; }"
"#status { font-weight: bold; color: #00ff00; margin-top: 20px; font-size: 18px; }"
".card { background: #333; padding: 20px; border-radius: 12px; display: inline-block; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }"
"</style></head><body>"
"<div class=\"card\">"
"<h2>Retro Console WAP</h2>"
"<input type=\"file\" id=\"fileInput\"><br>"
"<button onclick=\"uploadFile()\">Upload to SD Card</button>"
"<p id=\"status\"></p>"
"</div>"
"<script>"
"function uploadFile() {"
"  const file = document.getElementById('fileInput').files[0];"
"  if (!file) { alert('Select a file!'); return; }"
"  const status = document.getElementById('status');"
"  const xhr = new XMLHttpRequest();"
"  xhr.open('POST', '/upload?filename=' + encodeURIComponent(file.name), true);"
"  xhr.onload = function() {"
"    if (xhr.status == 200) status.innerText = 'Upload complete!';"
"    else status.innerText = 'Failed: ' + xhr.statusText;"
"  };"
"  xhr.upload.onprogress = function(e) {"
"    if (e.lengthComputable) {"
"      status.innerText = 'Uploading: ' + Math.round((e.loaded / e.total) * 100) + '%';"
"    }"
"  };"
"  xhr.send(file);"
"}"
"window.onload = function() {"
"  const now = new Date();"
"  const ts = Math.floor(now.getTime() / 1000) - (now.getTimezoneOffset() * 60);"
"  const xhr = new XMLHttpRequest();"
"  xhr.open('GET', '/sync_time?ts=' + ts, true);"
"  xhr.send();"
"};"
"</script>"
"</body></html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[256];
    char filename[128] = "uploaded_file.bin";
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        char *query = malloc(query_len);
        if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
            char encoded_filename[128];
            if (httpd_query_key_value(query, "filename", encoded_filename, sizeof(encoded_filename)) == ESP_OK) {
                urldecode2(filename, encoded_filename);
            }
        }
        free(query);
    }

    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);
    
    snprintf(wap_status_text, sizeof(wap_status_text), "Receiving %s...", filename);
    wap_progress = 0;
    ui_dirty = true;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        snprintf(wap_status_text, sizeof(wap_status_text), "Error: Cannot write to SD card");
        wap_progress = -1;
        ui_dirty = true;
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[1024];
    int received = 0;
    int remaining = req->content_len;
    while (remaining > 0) {
        if ((received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            fclose(f);
            snprintf(wap_status_text, sizeof(wap_status_text), "Error: Transfer failed");
            wap_progress = -1;
            ui_dirty = true;
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, f);
        remaining -= received;

        int percent = (int)(((float)(req->content_len - remaining) / req->content_len) * 100);
        if (percent / 5 > wap_progress / 5) { // Update UI every 5%
            wap_progress = percent;
            ui_dirty = true;
        }
    }
    fclose(f);

    httpd_resp_sendstr(req, "OK");
    
    snprintf(wap_status_text, sizeof(wap_status_text), "File saved: %s", filename);
    wap_progress = 100;
    ui_dirty = true;

    return ESP_OK;
}

#include "driver/i2c.h"

static uint8_t dec2bcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }

static void update_ds3231(time_t ts) {
    struct tm tinfo;
    localtime_r(&ts, &tinfo);
    uint8_t data[8];
    data[0] = 0x00; // register address
    data[1] = dec2bcd(tinfo.tm_sec);
    data[2] = dec2bcd(tinfo.tm_min);
    data[3] = dec2bcd(tinfo.tm_hour);
    data[4] = dec2bcd(tinfo.tm_wday + 1); // 1-7
    data[5] = dec2bcd(tinfo.tm_mday);
    data[6] = dec2bcd(tinfo.tm_mon + 1);
    data[7] = dec2bcd(tinfo.tm_year % 100);
    i2c_master_write_to_device(I2C_NUM_0, 0x68, data, 8, 1000 / portTICK_PERIOD_MS);
}

static esp_err_t sync_time_get_handler(httpd_req_t *req)
{
    char val[32];
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        char *query = malloc(query_len);
        if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
            if (httpd_query_key_value(query, "ts", val, sizeof(val)) == ESP_OK) {
                time_t ts = atol(val);
                struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                update_ds3231(ts);
                ESP_LOGI(TAG, "Time synced from WAP & DS3231 updated: %ld", (long)ts);
            }
        }
        free(query);
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t upload_uri = {
    .uri       = "/upload",
    .method    = HTTP_POST,
    .handler   = upload_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t time_uri = {
    .uri       = "/sync_time",
    .method    = HTTP_GET,
    .handler   = sync_time_get_handler,
    .user_ctx  = NULL
};

static void start_wap_server(void)
{
    if (!wifi_initialized) {
        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init failed");
        }
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_event_loop_create_default failed");
        }
        ap_netif = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        wifi_initialized = true;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "RetroConsole",
            .ssid_len = strlen("RetroConsole"),
            .channel = 6,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    // Start HTTP Server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &upload_uri);
        httpd_register_uri_handler(server, &time_uri);
        ESP_LOGI(TAG, "WAP Server started on port 80");
    }

    wap_progress = -1;
    snprintf(wap_status_text, sizeof(wap_status_text), "Waiting for connection...");
    snprintf(wap_ip_str, sizeof(wap_ip_str), "Connect to WiFi: RetroConsole\nIP: 192.168.4.1");
}

static void stop_wap_server(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    if (wifi_initialized) {
        esp_wifi_stop();
        // We do not deinit wifi completely to save time if reopened
    }
}


static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    rg_gui_draw_rect(x, y, w, h, fill_color);
}

static void draw_ui(void)
{
    rg_gui_clear(APP_BG);
    
    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 28, APP_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_center(SCREEN_W / 2, 6, "DOWNLOAD");
    rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    if (current_view == 0) {
        // Options List
        for (int i = 0; i < NUM_OPTIONS; i++) {
            int y = 50 + i * 40;
            bool is_sel = (i == selected_option);
            uint16_t bg = is_sel ? RG_COLOR_RGB(100, 60, 200) : APP_BG;

            if (is_sel) {
                draw_rounded_box(10, y, 220, 32, 6, bg, bg, 0);
            } else {
                rg_gui_draw_rect(10, y, 220, 32, APP_BG);
            }

            rg_gui_set_font_size(16);
            rg_gui_draw_text_center(SCREEN_W / 2, y + 8, options[i]);
        }
    } else if (current_view == 1) { // WAP View
        rg_gui_set_font_size(16);
        rg_gui_draw_text_center(SCREEN_W / 2, 50, "WAP");
        
        rg_gui_set_font_size(8);
        rg_gui_draw_text_center(SCREEN_W / 2, 80, "SSID: RetroConsole");
        rg_gui_draw_text_center(SCREEN_W / 2, 95, "IP: 192.168.4.1");
        
        // Status Box
        rg_gui_draw_rect(10, 130, 220, 60, RG_COLOR_RGB(30, 30, 30));
        
        if (wap_progress >= 0) {
            // Draw progress bar
            rg_gui_draw_rect(20, 170, 200, 10, RG_COLOR_RGB(50, 50, 50));
            int w = (wap_progress * 200) / 100;
            if (w > 0) rg_gui_draw_rect(20, 170, w, 10, RG_COLOR_RGB(0, 255, 0));
        }
        
        rg_gui_draw_text_center(SCREEN_W / 2, 145, wap_status_text);
        
        rg_gui_draw_text_center(SCREEN_W / 2, 280, "Press B/ESC to Stop");
    }
    
    rg_display_drain();
}

void app_wifi_init(void)
{
    ESP_LOGI(TAG, "Initialized Download app");
}

void app_wifi_start(void)
{
    ESP_LOGI(TAG, "Started Download app");
    selected_option = 0;
    current_view = 0;
    draw_ui();
}

void app_wifi_stop(void)
{
    if (current_view == 1) {
        stop_wap_server();
    }
    current_view = 0;
    ESP_LOGI(TAG, "Stopped Download app");
}

void app_wifi_handle_input(button_event_t event)
{
    if (current_view == 0) {
        if (event == BTN_UP || event == BTN_VOL_DOWN) {
            selected_option--;
            if (selected_option < 0) selected_option = NUM_OPTIONS - 1;
            draw_ui();
        } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
            selected_option++;
            if (selected_option >= NUM_OPTIONS) selected_option = 0;
            draw_ui();
        } else if (event == BTN_ENTER || event == BTN_A) {
            if (selected_option == 0) { // WAP
                current_view = 1;
                start_wap_server();
                draw_ui();
            } else {
                ESP_LOGI(TAG, "Selected %s - Not implemented", options[selected_option]);
            }
        }
    } else if (current_view == 1) {
        if (event == BTN_ESCAPE || event == BTN_B) {
            stop_wap_server();
            current_view = 0;
            draw_ui();
        }
    }
}

// Check for UI updates requested by the async HTTP server callbacks
void app_wifi_tick(void)
{
    if (ui_dirty && current_view == 1) {
        ui_dirty = false;
        draw_ui();
    }
}
