#include "input_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "board_pins_config.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "INPUT";

// PCNT Handle
static pcnt_unit_handle_t pcnt_unit = NULL;
static int last_enc_pos = 0;

// Debounce for buttons
static uint8_t last_button_state = 0xFF;
static int64_t last_button_time = 0;
#define DEBOUNCE_US 250000

static uint8_t read_shift_register(void)
{
    gpio_set_level(SR_PL_PIN, 0);
    esp_rom_delay_us(5);
    gpio_set_level(SR_PL_PIN, 1);

    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        int bit = gpio_get_level(SR_QH_PIN);
        data |= (bit << (7 - i));
        gpio_set_level(SR_CLK_PIN, 1);
        esp_rom_delay_us(5);
        gpio_set_level(SR_CLK_PIN, 0);
        esp_rom_delay_us(5);
    }
    return data;
}

void input_manager_init(void)
{
    gpio_config_t io_conf = {0};

    // PA_EN
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_48);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_48, 1);

    // 74HC165
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << SR_PL_PIN) | (1ULL << SR_CLK_PIN);
    gpio_config(&io_conf);
    gpio_set_level(SR_PL_PIN, 1);
    gpio_set_level(SR_CLK_PIN, 0);

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << SR_QH_PIN);
    gpio_config(&io_conf);

    // --- PCNT for KY-040 ---
    pcnt_unit_config_t unit_config = {
        .high_limit = 10000,
        .low_limit = -10000,
    };
    pcnt_new_unit(&unit_config, &pcnt_unit);

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENC_CLK_PIN,
        .level_gpio_num = ENC_DT_PIN,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a);

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENC_DT_PIN,
        .level_gpio_num = ENC_CLK_PIN,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b);

    pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // Configure pull-ups for encoder pins
    gpio_set_pull_mode(ENC_CLK_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(ENC_DT_PIN, GPIO_PULLUP_ONLY);

    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_clear_count(pcnt_unit);
    pcnt_unit_start(pcnt_unit);

    ESP_LOGI(TAG, "Input manager initialized (74HC165 + Hardware PCNT encoder)");
}

button_event_t input_manager_get_event(void)
{
    // --- 74HC165 Shift Register (buttons) ---
    uint8_t sr = read_shift_register();
    button_event_t evt = BTN_NONE;

    if (sr != 0xFF) {
        int64_t now = esp_timer_get_time();
        if (sr != last_button_state || (now - last_button_time) > DEBOUNCE_US) {
            last_button_state = sr;
            last_button_time = now;
            if (!(sr & (1 << 7))) evt = BTN_A;
            else if (!(sr & (1 << 6))) evt = BTN_UP;
            else if (!(sr & (1 << 5))) evt = BTN_DOWN;
            else if (!(sr & (1 << 4))) evt = BTN_LEFT;
            else if (!(sr & (1 << 3))) evt = BTN_B;
            else if (!(sr & (1 << 2))) evt = BTN_ESCAPE;
            else if (!(sr & (1 << 1))) evt = BTN_ENTER;
            else if (!(sr & (1 << 0))) evt = BTN_RIGHT;
        }
    } else {
        last_button_state = 0xFF;
    }

    // --- KY-040 Hardware PCNT ---
    int pcnt_count = 0;
    pcnt_unit_get_count(pcnt_unit, &pcnt_count);
    
    // PCNT registers 4 counts per full quadrature cycle (1 click)
    int encoder_clicks = pcnt_count / 4; 
    
    if (encoder_clicks > last_enc_pos) { 
        last_enc_pos = encoder_clicks; 
        evt = BTN_VOL_UP; 
    }
    else if (encoder_clicks < last_enc_pos) { 
        last_enc_pos = encoder_clicks; 
        evt = BTN_VOL_DOWN; 
    }

    return evt;
}
