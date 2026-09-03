#include "ui_display.h"
#include <stdio.h>
#include <stdbool.h>
#include "fsl_gpio.h"
#include "fsl_port.h"

// External dependencies for other modules
extern void sdcard_start_recording(void);
extern void sdcard_stop_recording(void);
extern void sdcard_play_selected(void);
extern void sdcard_next_file(void);
extern bool sdcard_is_inserted(void);

// RGB565 Colors
#define COLOR_BLUE  0x001F
#define COLOR_GREEN 0x07E0
#define COLOR_RED   0xF800
#define COLOR_BLACK 0x0000

static uint8_t active_mode = 0;
static bool redraw_needed = true;

// Mock SPI functions for ILI9341 (To be wired to real LPSPI in next steps)
void lcd_send_cmd(uint8_t cmd) {
    // GPIO_PinWrite(BOARD_LCD_DC_GPIO, BOARD_LCD_DC_PIN, 0);
    // LPSPI_WriteData(cmd);
}
void lcd_send_data(uint8_t data) {
    // GPIO_PinWrite(BOARD_LCD_DC_GPIO, BOARD_LCD_DC_PIN, 1);
    // LPSPI_WriteData(data);
}

void lcd_fill_screen(uint16_t color) {
    printf("LCD: Filling screen with color 0x%04X\n", color);
    /* Real ILI9341 fill sequence:
    lcd_send_cmd(0x2A); // Column Address Set
    // Send 0, 0, 320 >> 8, 320 & 0xFF
    lcd_send_cmd(0x2B); // Page Address Set
    // Send 0, 0, 240 >> 8, 240 & 0xFF
    lcd_send_cmd(0x2C); // Memory Write
    for(int i=0; i<320*240; i++) {
        lcd_send_data(color >> 8);
        lcd_send_data(color & 0xFF);
    }
    */
}

void ui_display_init(void) {
    printf("UI: Initializing ILI9341 SPI Display...\n");
    // Hardware Reset sequence
    // lcd_send_cmd(0x01); // Software reset
    // delay
    // lcd_send_cmd(0x11); // Sleep out
    // lcd_send_cmd(0x29); // Display ON
    lcd_fill_screen(COLOR_BLACK);
}

void ui_display_mode(uint8_t mode) {
    active_mode = mode;
    redraw_needed = true;
    printf("UI: Switched to mode %d\n", mode);
}

void ui_display_process_loop(void) {
    if (!redraw_needed) {
        return;
    }

    // Render the active mode GUI
    switch (active_mode) {
        case 0: // MODE_SYNTHESIZER
            lcd_fill_screen(COLOR_BLUE);
            printf("--- GUI: SYNTHESIZER MODE (BLUE BACKGROUND) ---\n");
            break;
            
        case 1: // MODE_SD_RECORDER_PLAYER
            lcd_fill_screen(COLOR_GREEN);
            printf("--- GUI: SD CARD RECORDER & PLAYER (GREEN BACKGROUND) ---\n");
            break;
            
        case 2: // MODE_ESP_WIFI
            lcd_fill_screen(COLOR_RED);
            printf("--- GUI: ESP WIFI SERVER MODE (RED BACKGROUND) ---\n");
            break;
    }
    
    redraw_needed = false;
}

void ui_handle_action1(uint8_t mode) {
    switch (mode) {
        case 0: printf("Action 1 Synth Mode\n"); break;
        case 1: printf("Action 1 SD Mode\n"); break;
        case 2: printf("Action 1 WiFi Mode\n"); break;
    }
}

void ui_handle_action2(uint8_t mode) {
    switch (mode) {
        case 0: printf("Action 2 Synth Mode\n"); break;
        case 1: printf("Action 2 SD Mode\n"); break;
        case 2: printf("Action 2 WiFi Mode\n"); break;
    }
}
