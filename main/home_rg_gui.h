#ifndef HOME_RG_GUI_H
#define HOME_RG_GUI_H

#include <stdint.h>
#include <stdbool.h>

void home_ui_init(void);
void home_ui_set_selected(uint8_t idx);
void home_ui_move_left(void);
void home_ui_move_right(void);
void home_ui_tick(void);
void home_ui_update(void);
void home_ui_draw(void);
int home_ui_get_selected(void);
void home_ui_force_redraw(void);

#endif
