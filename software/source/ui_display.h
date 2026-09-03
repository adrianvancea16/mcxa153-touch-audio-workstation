#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>

void ui_display_init(void);
void ui_display_mode(uint8_t mode);
void ui_display_process_loop(void);

// Button action handlers based on current mode
void ui_handle_action1(uint8_t mode);
void ui_handle_action2(uint8_t mode);

#endif // UI_DISPLAY_H
