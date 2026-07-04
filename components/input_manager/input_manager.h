#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <stdint.h>

typedef enum
{
    BTN_NONE,
    BTN_UP,       // D6 — 74HC165
    BTN_DOWN,     // D5 — 74HC165
    BTN_LEFT,     // D4 — 74HC165
    BTN_RIGHT,    // D0 — 74HC165
    BTN_ENTER,    // D1 — 74HC165 (OK/confirm)
    BTN_ESCAPE,   // D2 — 74HC165 (Back/cancel)
    BTN_A,        // D7 — 74HC165
    BTN_B,        // D3 — 74HC165
    BTN_VOL_UP,   // KY-040 encoder CW
    BTN_VOL_DOWN  // KY-040 encoder CCW
} button_event_t;

/**
 * @brief Initialize input manager.
 *        Sets up 74HC165 shift register (GPIO 3/6/7) and KY-040 rotary encoder (GPIO 1/2).
 */
void input_manager_init(void);

/**
 * @brief Poll for input events from shift register buttons and rotary encoder.
 * @return Button event if detected, BTN_NONE otherwise.
 */
button_event_t input_manager_get_event(void);

#endif