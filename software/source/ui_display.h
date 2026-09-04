#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H
#include <stdint.h>
#include <stdbool.h>

void ui_display_init(void);
void ui_display_process_loop(void);
void ui_display_mode(uint8_t mode);
void ui_handle_action1(uint8_t mode);
void ui_handle_action2(uint8_t mode);

#endif
