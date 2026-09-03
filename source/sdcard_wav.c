#include "sdcard_wav.h"
#include <stdio.h>

static bool is_inserted = false;

void sdcard_wav_init(void) {
    printf("SD Card: Initializing FatFS over SPI...\n");
    // Hardware check for SD card presence (CD pin if available, or SPI init success)
    is_inserted = true; // Assume inserted for testing
}

void sdcard_wav_process_loop(void) {
    // Background tasks for SD card:
    // e.g. flushing WAV buffers periodically during recording
}

bool sdcard_is_inserted(void) {
    return is_inserted;
}

void sdcard_start_recording(void) {
    printf("SD Card: Starting WAV recording...\n");
}

void sdcard_stop_recording(void) {
    printf("SD Card: Stopped WAV recording. File saved.\n");
}

void sdcard_play_selected(void) {
    printf("SD Card: Playing selected WAV file to Headphones...\n");
}

void sdcard_next_file(void) {
    printf("SD Card: Selected next file in list.\n");
}
