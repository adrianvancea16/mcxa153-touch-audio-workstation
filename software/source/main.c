#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_reset.h"
#include "fsl_common.h"

#include "ui_display.h"
#include "audio_engine.h"
#include "sdcard_wav.h"
#include "wifi_esp.h"

typedef enum {
    MODE_SYNTH = 0,
    MODE_SD = 1,
    MODE_WIFI = 2,
    MODE_COUNT
} app_mode_t;

volatile app_mode_t current_mode = MODE_SYNTH;

#define APP_SW1_GPIO GPIO2
#define APP_SW1_PORT PORT2
#define APP_SW1_PIN  2U

#define APP_SW2_GPIO GPIO3
#define APP_SW2_PORT PORT3
#define APP_SW2_PIN  13U

#define APP_SW3_GPIO GPIO3
#define APP_SW3_PORT PORT3
#define APP_SW3_PIN  14U

bool sw1_pressed = false;
bool sw2_pressed = false;
bool sw3_pressed = false;

void check_buttons(void) {
    

    if (GPIO_PinRead(APP_SW1_GPIO, APP_SW1_PIN) == 0) {
        
        if (!sw1_pressed) {
            sw1_pressed = true;
            SDK_DelayAtLeastUs(50000U, SystemCoreClock); // debounce
            current_mode = (app_mode_t)((current_mode + 1) % MODE_COUNT);
            ui_display_mode(current_mode);
        }
    } else { sw1_pressed = false; }

    if (GPIO_PinRead(APP_SW2_GPIO, APP_SW2_PIN) == 0) {
        
        if (!sw2_pressed) {
            sw2_pressed = true;
            SDK_DelayAtLeastUs(50000U, SystemCoreClock);
            ui_handle_action1(current_mode);
        }
    } else { sw2_pressed = false; }

    if (GPIO_PinRead(APP_SW3_GPIO, APP_SW3_PIN) == 0) {
        
        if (!sw3_pressed) {
            sw3_pressed = true;
            SDK_DelayAtLeastUs(50000U, SystemCoreClock);
            ui_handle_action2(current_mode);
        }
    } else { sw3_pressed = false; }
    
    // Draw visual feedback for buttons on the top right
}

int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    CLOCK_EnableClock(kCLOCK_GatePORT1); CLOCK_EnableClock(kCLOCK_GateGPIO1);
    CLOCK_EnableClock(kCLOCK_GatePORT2); CLOCK_EnableClock(kCLOCK_GateGPIO2);
    CLOCK_EnableClock(kCLOCK_GatePORT3); CLOCK_EnableClock(kCLOCK_GateGPIO3);

    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn); RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT2_RST_SHIFT_RSTn); RESET_ReleasePeripheralReset(kGPIO2_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn); RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);

    gpio_pin_config_t in_config = {kGPIO_DigitalInput, 0};
    port_pin_config_t pull_up = {0};
    pull_up.pullSelect = kPORT_PullUp;
    pull_up.mux = kPORT_MuxAlt0;
    // CRITICAL FIX: The Input Buffer must be enabled for GPIO_PinRead to work on MCXA!
    pull_up.inputBuffer = kPORT_InputBufferEnable; 
    
    PORT_SetPinConfig(PORT2, 2U, &pull_up); GPIO_PinInit(GPIO2, 2U, &in_config);
    PORT_SetPinConfig(PORT3, 13U, &pull_up); GPIO_PinInit(GPIO3, 13U, &in_config);
    PORT_SetPinConfig(PORT3, 14U, &pull_up); GPIO_PinInit(GPIO3, 14U, &in_config);

    printf("Starting Proper Audio Workstation...\n");
    ui_display_init();
    AudioEngine_Init();
    sdcard_wav_init();
    wifi_esp_init();

    ui_display_mode(current_mode);

    while (1) {
        check_buttons();
        ui_display_process_loop();
        AudioEngine_Process();
        sdcard_wav_process_loop();
        wifi_esp_process_loop();
    }
    return 0;
}
