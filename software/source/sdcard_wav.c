#include "sdcard_wav.h"
#include "ff.h"
#include "audio_engine.h"
#include "demo_mode.h"
#include <stdio.h>
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

#if !DEMO_MODE
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
#else
/* Simulated card state. Every public getter below answers from this instead of
 * touching FatFS, so the recorder screen can be shown while the real SD
 * bring-up is still unfinished. See demo_mode.h. The numbers are chosen to
 * behave like a real card would: the clock runs while recording, free space
 * ticks down at the true byte rate of this WAV format, and stopping a
 * recording adds a file to the list. */
#define DEMO_FREE_KB_AT_BOOT 1958400UL /* ~1.9GB, a plausible 2GB card */
#define DEMO_WAV_KB_PER_SEC  ((WAV_SAMPLE_RATE * WAV_NUM_CHANNELS * (WAV_BITS_PER_SAMPLE / 8)) / 1024U)

static bool     s_demo_recording = false;
static bool     s_demo_playing   = false;
static uint32_t s_demo_rec_start_ms = 0;
static uint32_t s_demo_rec_total_ms = 0; /* accumulated across finished takes */
static uint16_t s_demo_count    = 3;
static uint16_t s_demo_selected = 1;
static char     s_demo_name[13];

static uint32_t DemoRecElapsedMs(void) {
    return s_demo_recording ? (AudioEngine_GetUptimeMs() - s_demo_rec_start_ms) : 0U;
}
#endif

/* --- Directory scanning helpers (no LFN, plain 8.3 "RECnnnn.WAV" names) --- */
#if !DEMO_MODE

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

#endif /* !DEMO_MODE */

#if !DEMO_MODE
/* Cached because f_getfree() is far from free. FatFS keeps a free-cluster
 * count, but every write invalidates it and the next call walks the whole FAT
 * again -- over bit-banged SPI on a 2GB card that is seconds. This function is
 * read by the recorder screen *and* by every /status request, so uncached it
 * would rescan the FAT on each poll, right while recording is writing. Refresh
 * on mount and when a recording closes; in between, subtract what the open
 * file has written, which is exact for our fixed-rate PCM. */
static uint32_t s_free_kb_cache = 0;

static void RefreshFreeSpace(void) {
    s_free_kb_cache = 0;
    if (!s_mounted) return;
    DWORD freeClusters;
    FATFS *fs = &s_fatfs;
    if (f_getfree("0:/", &freeClusters, &fs) != FR_OK) return;
    uint64_t freeBytes = (uint64_t)freeClusters * fs->csize * 512ULL;
    s_free_kb_cache = (uint32_t)(freeBytes / 1024ULL);
}
#endif

/* --- Mount / detection --- */

void sdcard_wav_init(void) {
#if !DEMO_MODE
    s_mounted = false;
#endif
    sdcard_recheck();
}

void sdcard_recheck(void) {
#if DEMO_MODE
    return; /* the simulated card is always present; nothing to re-mount */
#else
    if (s_recording || s_playing) return; /* don't yank the card out from under an open file */
    f_mount(NULL, "0:/", 0); /* force a clean re-init on the next f_mount */
    FRESULT r = f_mount(&s_fatfs, "0:/", 1);
    s_mounted = (r == FR_OK);
    if (s_mounted) { RefreshSelection(); RefreshFreeSpace(); }
#endif
}

bool sdcard_is_inserted(void) {
#if DEMO_MODE
    return true;
#else
    return s_mounted;
#endif
}


uint32_t sdcard_free_space_kb(void) {
#if DEMO_MODE
    uint32_t usedKb = ((s_demo_rec_total_ms + DemoRecElapsedMs()) / 1000U) * DEMO_WAV_KB_PER_SEC;
    return (usedKb >= DEMO_FREE_KB_AT_BOOT) ? 0U : (DEMO_FREE_KB_AT_BOOT - usedKb);
#else
    if (!s_mounted) return 0;
    uint32_t writtenKb = (s_rec_samples * (WAV_BITS_PER_SAMPLE / 8U)) / 1024U;
    return (writtenKb >= s_free_kb_cache) ? 0U : (s_free_kb_cache - writtenKb);
#endif
}

/* --- Recording --- */

bool sdcard_is_recording(void) {
#if DEMO_MODE
    return s_demo_recording;
#else
    return s_recording;
#endif
}

uint32_t sdcard_record_elapsed_ms(void) {
#if DEMO_MODE
    return DemoRecElapsedMs();
#else
    return (s_rec_samples * 1000U) / WAV_SAMPLE_RATE;
#endif
}

void sdcard_start_recording(void) {
#if DEMO_MODE
    if (s_demo_recording || s_demo_playing) return;
    s_demo_rec_start_ms = AudioEngine_GetUptimeMs();
    s_demo_recording = true;
    return;
#else
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
#endif
}

void sdcard_stop_recording(void) {
#if DEMO_MODE
    if (!s_demo_recording) return;
    s_demo_rec_total_ms += DemoRecElapsedMs();
    s_demo_recording = false;
    /* A finished take shows up as a new file, so the list grows on screen
     * exactly as it would with a real card. */
    if (s_demo_count < 9999) s_demo_count++;
    s_demo_selected = s_demo_count;
    return;
#else
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
    RefreshFreeSpace();
#endif
}

/* --- Playback --- */

bool sdcard_is_playing(void) {
#if DEMO_MODE
    return s_demo_playing;
#else
    return s_playing;
#endif
}

void sdcard_start_playback(void) {
#if DEMO_MODE
    /* Deliberately does not switch the audio engine to SD playback: there is
     * no file to feed the ring buffer, so that would just mute the board.
     * Leaving it monitoring the mic keeps the demo audible. */
    if (s_demo_recording) return;
    s_demo_playing = true;
    return;
#else
    if (!s_mounted || s_recording || s_playing) return;
    if (s_selected_name[0] == 0) return;
    if (f_open(&s_file, s_selected_name, FA_READ) != FR_OK) return;
    f_lseek(&s_file, sizeof(WavHeader_t)); /* skip the header we wrote */
    s_playing = true;
    AudioEngine_SetMode(AUDIO_MODE_SD_PLAYBACK);
    AudioEngine_SetPlaybackEnable(true);
#endif
}

void sdcard_stop_playback(void) {
#if DEMO_MODE
    s_demo_playing = false;
    return;
#else
    if (!s_playing) return;
    AudioEngine_SetPlaybackEnable(false);
    AudioEngine_SetMode(AUDIO_MODE_SYNTH);
    f_close(&s_file);
    s_playing = false;
#endif
}

void sdcard_next_file(void) {
#if DEMO_MODE
    s_demo_selected = (uint16_t)((s_demo_selected % s_demo_count) + 1);
    return;
#else
    uint16_t count;
    ScanRecordings(0, NULL, &count, NULL);
    if (count == 0) { s_selected_number = 0; s_selected_name[0] = 0; return; }
    s_selected_number = (uint16_t)((s_selected_number % count) + 1);
    ScanRecordings(s_selected_number, s_selected_name, NULL, NULL);
#endif
}

const char *sdcard_current_filename(void) {
#if DEMO_MODE
    snprintf(s_demo_name, sizeof(s_demo_name), "REC%04u.WAV", s_demo_selected);
    return s_demo_name;
#else
    return s_selected_name[0] ? s_selected_name : "---";
#endif
}

uint16_t sdcard_file_count(void) {
#if DEMO_MODE
    return s_demo_count;
#else
    uint16_t c; ScanRecordings(0, NULL, &c, NULL); return c;
#endif
}

uint16_t sdcard_file_selected_number(void) {
#if DEMO_MODE
    return s_demo_selected;
#else
    return s_selected_number;
#endif
}

/* --- Main-loop pump: move samples between the ring buffers and the open file
 * in small chunks so a single call never blocks for long. --- */
void sdcard_wav_process_loop(void) {
#if DEMO_MODE
    return; /* nothing is actually open, so there is nothing to pump */
#else
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
#endif
}
