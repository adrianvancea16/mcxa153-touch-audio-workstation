#include "ui_display.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Mock colors
#define COLOR_BG    0x0000 // Black
#define COLOR_TEXT  0xFFFF // White
#define COLOR_FFT   0x07E0 // Green
#define COLOR_WAVE  0x07FF // Cyan
#define COLOR_UI    0xF800 // Red

static uint8_t active_mode = 0;
static bool redraw_needed = true;

// Mock SPI functions
void lcd_send_cmd(uint8_t cmd) {}
void lcd_send_data(uint8_t data) {}

// Basic Graphics Primitives
void draw_pixel(int x, int y, uint16_t color) {
    // In real hardware: Set column addr (x, x), page addr (y, y), write memory
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    // In real hardware: optimized block memory write
}

void draw_string(int x, int y, const char* str, uint16_t color) {
    // In real hardware: iterate through characters and draw 8x8 font bitmaps
    printf("DRAW TEXT [%d, %d]: %s\n", x, y, str);
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    // Bresenham's line algorithm
}

// GUI Rendering Functions
void draw_synthesizer_ui(void) {
    draw_rect(0, 0, 320, 240, COLOR_BG);
    draw_string(10, 10, "MODE: SYNTHESIZER & FFT", COLOR_TEXT);
    
    // Draw Oscilloscope Box
    draw_rect(10, 30, 300, 80, 0x18E3); // Dark gray bg
    draw_string(15, 35, "Mic Waveform", COLOR_TEXT);
    // Draw mock sine wave
    for(int i=0; i<290; i+=5) {
        draw_line(15+i, 70, 20+i, 70 + (i%20 == 0 ? 20 : -20), COLOR_WAVE);
    }

    // Draw FFT Spectrum Box
    draw_rect(10, 120, 300, 100, 0x18E3);
    draw_string(15, 125, "FFT Spectrum Analysis", COLOR_TEXT);
    // Draw mock FFT bars
    for(int i=0; i<28; i++) {
        int bar_height = rand() % 70;
        draw_rect(20 + i*10, 215 - bar_height, 8, bar_height, COLOR_FFT);
    }
}

void draw_sd_recorder_ui(void) {
    draw_rect(0, 0, 320, 240, COLOR_BG);
    draw_string(10, 10, "MODE: SD CARD RECORDER", COLOR_TEXT);
    
    bool has_sd = true; // Assume inserted for UI preview
    if(has_sd) {
        draw_string(10, 30, "STATUS: SD Card Inserted (WAV)", COLOR_FFT);
        draw_string(10, 50, "--- RECORDINGS LIST ---", COLOR_TEXT);
        
        // Draw Mock List
        draw_string(15, 70, "1. VOCAL_01.WAV", COLOR_TEXT);
        draw_rect(10, 68, 300, 15, COLOR_UI); // Highlight selected
        draw_string(15, 90, "2. SYNTH_02.WAV", COLOR_TEXT);
        draw_string(15, 110, "3. BASS_03.WAV", COLOR_TEXT);
        
        draw_string(10, 200, "Btn2: Record | Btn3: Play Selected", COLOR_WAVE);
    } else {
        draw_string(10, 30, "STATUS: NO SD CARD DETECTED!", COLOR_UI);
    }
}

void draw_wifi_server_ui(void) {
    draw_rect(0, 0, 320, 240, COLOR_BG);
    draw_string(10, 10, "MODE: ESP WI-FI SERVER", COLOR_TEXT);
    
    draw_string(10, 50, "Status: Connected to AP", COLOR_FFT);
    draw_string(10, 70, "IP Address: 192.168.0.105", COLOR_TEXT);
    draw_string(10, 90, "Port: 80", COLOR_TEXT);
    
    draw_rect(10, 120, 300, 80, 0x18E3);
    draw_string(15, 125, "Server Logs:", COLOR_TEXT);
    draw_string(15, 145, "> Client connected...", COLOR_WAVE);
    draw_string(15, 165, "> Telemetry streaming OK.", COLOR_WAVE);
}

void ui_display_init(void) {
    printf("UI: Initializing ILI9341 & Graphics Engine...\n");
}

void ui_display_mode(uint8_t mode) {
    active_mode = mode;
    redraw_needed = true;
}

void ui_display_process_loop(void) {
    if (!redraw_needed) {
        return; // Only redraw when mode changes or data updates heavily
    }

    switch (active_mode) {
        case 0: draw_synthesizer_ui(); break;
        case 1: draw_sd_recorder_ui(); break;
        case 2: draw_wifi_server_ui(); break;
    }
    
    redraw_needed = false;
}

void ui_handle_action1(uint8_t mode) {
    // Force redraw on action to simulate UI reaction
    redraw_needed = true; 
}

void ui_handle_action2(uint8_t mode) {
    redraw_needed = true;
}
