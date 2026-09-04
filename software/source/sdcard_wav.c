#include "sdcard_wav.h"
#include "ff.h"
#include "audio_engine.h"
#include <string.h>

#define WAV_SAMPLE_RATE  AUDIO_SAMPLE_RATE_HZ
#define WAV_NUM_CHANNELS 1U
#define WAV_BITS_PER_SAMPLE 16U
#define REC_CHUNK_SAMPLES 128U   /* how many samples we drain/feed per main-loop pass */

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t chunkSize;
    char     wave[4];
    char     fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     data[4];
    uint32_t dataSize;
} WavHeader_t;

static FATFS s_fatfs;
static FIL   s_file;
static bool  s_mounted = false;

static bool     s_recording = false;
static uint32_t s_rec_samples = 0;
static char     s_rec_name[13];

static bool     s_playing = false;
static uint16_t s_selected_number = 1; /* 1-based; 0 means "no files" */
static char     s_selected_name[13] = {0};

static int16_t s_chunk_buf[REC_CHUNK_SAMPLES];

/* --- Directory scanning helpers (no LFN, plain 8.3 "RECnnnn.WAV" names) --- */

static bool ParseRecName(const char *name, uint16_t *outIdx) {
    if (strlen(name) < 8) return false;
    if (name[0] != 'R' || name[1] != 'E' || name[2] != 'C') return false;
    uint16_t idx = 0;
    for (int d = 0; d < 4; d++) {
        char c = name[3 + d];
        if (c < '0' || c > '9') return false;
        idx = (uint16_t)(idx * 10 + (c - '0'));
    }
    if (strcmp(name + 7, ".WAV") != 0) return false;
    *outIdx = idx;
    return true;
}

/* Scans the root dir once. Always fills outCount/outMaxIdx; if wantPosition
 * (1-based) is within range, also fills outName. */
static void ScanRecordings(uint16_t wantPosition, char *outName, uint16_t *outCount, uint16_t *outMaxIdx) {
    DIR dir;
    FILINFO fno;
    uint16_t count = 0, maxIdx = 0;
    if (outName) outName[0] = 0;
    if (f_opendir(&dir, "0:/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            uint16_t idx;
            if (!(fno.fattrib & AM_DIR) && ParseRecName(fno.fname, &idx)) {
                count++;
                if (idx > maxIdx) maxIdx = idx;
                if (outName && count == wantPosition) strncpy(outName, fno.fname, 12);
            }
        }
        f_closedir(&dir);
    }
    if (outCount) *outCount = count;
    if (outMaxIdx) *outMaxIdx = maxIdx;
}

static void RefreshSelection(void) {
    uint16_t count = 0;
    if (s_selected_number == 0) s_selected_number = 1;
    ScanRecordings(s_selected_number, s_selected_name, &count, NULL);
    if (count == 0) { s_selected_name[0] = 0; s_selected_number = 0; }
    else if (s_selected_number > count) { s_selected_number = count; ScanRecordings(count, s_selected_name, NULL, NULL); }
}

/* --- Mount / detection --- */

void sdcard_wav_init(void) {
    s_mounted = false;
    sdcard_recheck();
}

void sdcard_recheck(void) {
    if (s_recording || s_playing) return; /* don't yank the card out from under an open file */
    f_mount(NULL, "0:/", 0); /* force a clean re-init on the next f_mount */
    FRESULT r = f_mount(&s_fatfs, "0:/", 1);
    s_mounted = (r == FR_OK);
    if (s_mounted) RefreshSelection();
}

bool sdcard_is_inserted(void) { return s_mounted; }

uint32_t sdcard_free_space_kb(void) {
    if (!s_mounted) return 0;
    DWORD freeClusters;
    FATFS *fs = &s_fatfs;
    if (f_getfree("0:/", &freeClusters, &fs) != FR_OK) return 0;
    uint64_t freeBytes = (uint64_t)freeClusters * fs->csize * 512ULL;
    return (uint32_t)(freeBytes / 1024ULL);
}

/* --- Recording --- */

bool sdcard_is_recording(void) { return s_recording; }

uint32_t sdcard_record_elapsed_ms(void) {
    return (s_rec_samples * 1000U) / WAV_SAMPLE_RATE;
}

void sdcard_start_recording(void) {
    if (!s_mounted || s_recording || s_playing) return;

    uint16_t count, maxIdx;
    ScanRecordings(0, NULL, &count, &maxIdx);
    uint16_t nextIdx = (uint16_t)(maxIdx + 1);
    if (nextIdx > 9999) nextIdx = 1; /* wrap, extremely unlikely to matter in practice */
    s_rec_name[0] = 'R'; s_rec_name[1] = 'E'; s_rec_name[2] = 'C';
    s_rec_name[3] = (char)('0' + (nextIdx / 1000) % 10);
    s_rec_name[4] = (char)('0' + (nextIdx / 100) % 10);
    s_rec_name[5] = (char)('0' + (nextIdx / 10) % 10);
    s_rec_name[6] = (char)('0' + nextIdx % 10);
    strcpy(s_rec_name + 7, ".WAV");

    if (f_open(&s_file, s_rec_name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;

    WavHeader_t hdr = {
        .riff = {'R','I','F','F'}, .chunkSize = 36, .wave = {'W','A','V','E'},
        .fmt = {'f','m','t',' '}, .fmtSize = 16, .audioFormat = 1,
        .numChannels = WAV_NUM_CHANNELS, .sampleRate = WAV_SAMPLE_RATE,
        .byteRate = WAV_SAMPLE_RATE * WAV_NUM_CHANNELS * (WAV_BITS_PER_SAMPLE / 8),
        .blockAlign = (uint16_t)(WAV_NUM_CHANNELS * (WAV_BITS_PER_SAMPLE / 8)),
        .bitsPerSample = WAV_BITS_PER_SAMPLE, .data = {'d','a','t','a'}, .dataSize = 0,
    };
    UINT bw;
    f_write(&s_file, &hdr, sizeof(hdr), &bw);

    s_rec_samples = 0;
    s_recording = true;
    AudioEngine_SetRecordEnable(true);
}

void sdcard_stop_recording(void) {
    if (!s_recording) return;
    AudioEngine_SetRecordEnable(false);

    /* Drain anything still sitting in the ring buffer before closing. */
    int16_t leftover;
    do {
        leftover = 0;
        uint16_t got = AudioEngine_RecordRead(s_chunk_buf, REC_CHUNK_SAMPLES);
        if (got > 0) {
            UINT bw;
            f_write(&s_file, s_chunk_buf, (UINT)(got * sizeof(int16_t)), &bw);
            s_rec_samples += got;
            leftover = (int16_t)got;
        }
    } while (leftover > 0);

    uint32_t dataSize = s_rec_samples * WAV_NUM_CHANNELS * (WAV_BITS_PER_SAMPLE / 8);
    uint32_t chunkSize = 36 + dataSize;
    f_lseek(&s_file, 4);
    UINT bw;
    f_write(&s_file, &chunkSize, 4, &bw);
    f_lseek(&s_file, 40);
    f_write(&s_file, &dataSize, 4, &bw);
    f_close(&s_file);

    s_recording = false;
    RefreshSelection();
}

/* --- Playback --- */

bool sdcard_is_playing(void) { return s_playing; }

void sdcard_start_playback(void) {
    if (!s_mounted || s_recording || s_playing) return;
    if (s_selected_name[0] == 0) return;
    if (f_open(&s_file, s_selected_name, FA_READ) != FR_OK) return;
    f_lseek(&s_file, sizeof(WavHeader_t)); /* skip the header we wrote */
    s_playing = true;
    AudioEngine_SetMode(AUDIO_MODE_SD_PLAYBACK);
    AudioEngine_SetPlaybackEnable(true);
}

void sdcard_stop_playback(void) {
    if (!s_playing) return;
    AudioEngine_SetPlaybackEnable(false);
    AudioEngine_SetMode(AUDIO_MODE_SYNTH);
    f_close(&s_file);
    s_playing = false;
}

void sdcard_next_file(void) {
    uint16_t count;
    ScanRecordings(0, NULL, &count, NULL);
    if (count == 0) { s_selected_number = 0; s_selected_name[0] = 0; return; }
    s_selected_number = (uint16_t)((s_selected_number % count) + 1);
    ScanRecordings(s_selected_number, s_selected_name, NULL, NULL);
}

const char *sdcard_current_filename(void) { return s_selected_name[0] ? s_selected_name : "---"; }
uint16_t sdcard_file_count(void) { uint16_t c; ScanRecordings(0, NULL, &c, NULL); return c; }
uint16_t sdcard_file_selected_number(void) { return s_selected_number; }

/* --- Main-loop pump: move samples between the ring buffers and the open file
 * in small chunks so a single call never blocks for long. --- */
void sdcard_wav_process_loop(void) {
    if (s_recording) {
        uint16_t avail = AudioEngine_RecordAvailable();
        if (avail >= REC_CHUNK_SAMPLES) {
            uint16_t got = AudioEngine_RecordRead(s_chunk_buf, REC_CHUNK_SAMPLES);
            UINT bw;
            f_write(&s_file, s_chunk_buf, (UINT)(got * sizeof(int16_t)), &bw);
            s_rec_samples += got;
            if (bw != got * sizeof(int16_t)) {
                /* card full or write error -- stop gracefully instead of losing the file */
                sdcard_stop_recording();
            }
        }
    }

    if (s_playing) {
        uint16_t space = AudioEngine_PlaybackFree();
        if (space >= REC_CHUNK_SAMPLES) {
            UINT br = 0;
            f_read(&s_file, s_chunk_buf, REC_CHUNK_SAMPLES * sizeof(int16_t), &br);
            uint16_t gotSamples = (uint16_t)(br / sizeof(int16_t));
            if (gotSamples == 0) {
                sdcard_stop_playback(); /* end of file */
            } else {
                AudioEngine_PlaybackWrite(s_chunk_buf, gotSamples);
            }
        }
    }
}
