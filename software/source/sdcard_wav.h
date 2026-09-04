#ifndef SDCARD_WAV_H
#define SDCARD_WAV_H

#include <stdint.h>
#include <stdbool.h>

/* One-time hardware bring-up (SD_CS pin, shared SPI bus pins). Safe to call
 * even if no card is inserted -- sdcard_is_inserted() will simply read false. */
void sdcard_wav_init(void);

/* Call every iteration of the main superloop: pumps chunked f_write()/f_read()
 * for an active recording/playback so neither blocks the rest of the system
 * for long. */
void sdcard_wav_process_loop(void);

/* Re-attempts f_mount(). Call when entering the SD Recorder screen (there is
 * no card-detect pin on this board, so mount success/failure IS the detection
 * mechanism) and after removing/reinserting a card. */
void sdcard_recheck(void);

bool sdcard_is_inserted(void);
uint32_t sdcard_free_space_kb(void);

/* No-ops if !sdcard_is_inserted() -- recording is only possible with a real,
 * mounted card, by design. */
void sdcard_start_recording(void);
void sdcard_stop_recording(void);
bool sdcard_is_recording(void);
uint32_t sdcard_record_elapsed_ms(void);

void sdcard_start_playback(void);
void sdcard_stop_playback(void);
bool sdcard_is_playing(void);

void sdcard_next_file(void);
const char *sdcard_current_filename(void);
uint16_t sdcard_file_count(void);
uint16_t sdcard_file_selected_number(void); /* 1-based position for "N / M" display */

#endif /* SDCARD_WAV_H */
