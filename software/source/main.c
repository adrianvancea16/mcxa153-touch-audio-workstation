#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

// Includes for project modules
#include "ui_display.h"
#include "audio_dsp.h"
#include "sdcard_wav.h"
#include "wifi_esp.h"

// Define the 3 modes
typedef enum {
    MODE_SYNTHESIZER = 0,
    MODE_SD_RECORDER_PLAYER = 1,
    MODE_ESP_WIFI = 2,
    MODE_COUNT
} app_mode_t;

volatile app_mode_t current_mode = MODE_SYNTHESIZER;

// Button definitions
#define APP_SW1_GPIO GPIO2
#define APP_SW1_PORT PORT2
#define APP_SW1_PIN  2U

#define APP_SW2_GPIO GPIO3
#define APP_SW2_PORT PORT3
#define APP_SW2_PIN  13U

#define APP_SW3_GPIO GPIO3
#define APP_SW3_PORT PORT3
#define APP_SW3_PIN  14U

// Simple delay function for debounce (to be replaced with timer in production)
void delay_ms(uint32_t ms) {
    uint32_t loops = ms * (SystemCoreClock / 10000); // Approximate
    while (loops--) {
        __NOP();
    }
}

// Button state tracking
bool sw1_pressed = false;
bool sw2_pressed = false;
bool sw3_pressed = false;

void check_buttons(void) {
    // Read SW1 (Mode Navigation)
    if (GPIO_PinRead(APP_SW1_GPIO, APP_SW1_PIN) == 0) {
        if (!sw1_pressed) {
            sw1_pressed = true;
            delay_ms(50); // Debounce
            
            // Navigate to next mode
            current_mode = (app_mode_t)((current_mode + 1) % MODE_COUNT);
            ui_display_mode(current_mode);
        }
    } else {
        sw1_pressed = false;
    }

    // Read SW2 (Action 1)
    if (GPIO_PinRead(APP_SW2_GPIO, APP_SW2_PIN) == 0) {
        if (!sw2_pressed) {
            sw2_pressed = true;
            delay_ms(50); // Debounce
            ui_handle_action1(current_mode);
        }
    } else {
        sw2_pressed = false;
    }

    // Read SW3 (Action 2)
    if (GPIO_PinRead(APP_SW3_GPIO, APP_SW3_PIN) == 0) {
        if (!sw3_pressed) {
            sw3_pressed = true;
            delay_ms(50); // Debounce
            ui_handle_action2(current_mode);
        }
    } else {
        sw3_pressed = false;
    }
}

int main(void) {
    // Board Initialization
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    printf("Starting NXP Audio Workstation...\n");

    // Initialize subsystems
    ui_display_init();
    audio_dsp_init();
    sdcard_wav_init();
    wifi_esp_init();

    // Show initial mode
    ui_display_mode(current_mode);

    while (1) {
        // Poll buttons for navigation and actions
        check_buttons();

        // Run mode-specific background tasks
        switch (current_mode) {
            case MODE_SYNTHESIZER:
                audio_dsp_process_loop();
                break;
            case MODE_SD_RECORDER_PLAYER:
                sdcard_wav_process_loop();
                break;
            case MODE_ESP_WIFI:
                wifi_esp_process_loop();
                break;
            default:
                break;
        }

        // Run UI update loop
        ui_display_process_loop();
    }
    return 0;
}
