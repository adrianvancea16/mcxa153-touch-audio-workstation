#ifndef SDCARD_WAV_H
#define SDCARD_WAV_H

#include <stdbool.h>

void sdcard_wav_init(void);
void sdcard_wav_process_loop(void);

bool sdcard_is_inserted(void);
void sdcard_start_recording(void);
void sdcard_stop_recording(void);
void sdcard_play_selected(void);
void sdcard_next_file(void);

#endif // SDCARD_WAV_H
