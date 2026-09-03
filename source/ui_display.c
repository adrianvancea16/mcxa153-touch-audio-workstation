#include "ui_display.h"
#include <stdio.h>
#include <stdbool.h>

// External dependencies for other modules
extern void sdcard_start_recording(void);
extern void sdcard_stop_recording(void);
extern void sdcard_play_selected(void);
extern void sdcard_next_file(void);
extern bool sdcard_is_inserted(void);

static uint8_t active_mode = 0;
static bool redraw_needed = true;

void ui_display_init(void) {
    // Initialize ILI9341 SPI Display and touch controller here
    printf("UI: Display initialized.\n");
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
            printf("--- GUI: SYNTHESIZER MODE ---\n");
            printf("Visualizing FFT Spectrum & Oscilloscope...\n");
            printf("Button 2: Change Synth Effect | Button 3: Toggle Output\n");
            break;
            
        case 1: // MODE_SD_RECORDER_PLAYER
            printf("--- GUI: SD CARD RECORDER & PLAYER ---\n");
            if (sdcard_is_inserted()) {
                printf("SD Card: Inserted. Ready.\n");
                printf("Button 2: Record to WAV | Button 3: Play Next File\n");
            } else {
                printf("SD Card: NOT INSERTED. Please insert SD Card.\n");
            }
            break;
            
        case 2: // MODE_ESP_WIFI
            printf("--- GUI: ESP WIFI SERVER MODE ---\n");
            printf("Connecting to Network...\n");
            printf("Server IP: 192.168.1.X\n");
            printf("Button 2: Reconnect | Button 3: Send Test Data\n");
            break;
    }
    
    redraw_needed = false;
}

void ui_handle_action1(uint8_t mode) {
    // Button 2 pressed
    switch (mode) {
        case 0:
            printf("Action 1 in Synth Mode: Changing synth effect...\n");
            break;
        case 1:
            if (sdcard_is_inserted()) {
                printf("Action 1 in SD Mode: Start/Stop Recording to WAV.\n");
                // Toggle recording
                // sdcard_start_recording();
            }
            break;
        case 2:
            printf("Action 1 in WiFi Mode: Reconnecting ESP...\n");
            break;
    }
}

void ui_handle_action2(uint8_t mode) {
    // Button 3 pressed
    switch (mode) {
        case 0:
            printf("Action 2 in Synth Mode: Toggling Audio Output...\n");
            break;
        case 1:
            if (sdcard_is_inserted()) {
                printf("Action 2 in SD Mode: Playing next WAV from list...\n");
                // sdcard_play_selected();
            }
            break;
        case 2:
            printf("Action 2 in WiFi Mode: Sending Test Telemetry...\n");
            break;
    }
}
