#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

#define FFT_SIZE 256
#define AUDIO_SAMPLE_RATE_HZ 16000U

/* Streaming ring buffers used to move samples to/from the SD card without
 * needing a large RAM buffer for the whole recording (that used to eat
 * ~16KB of the 24KB SRAM this chip has). sdcard_wav.c drains/fills these
 * from the main superloop while AudioEngine_Process() (driven by the
 * SysTick-derived sample-rate flag) fills/drains them in real time. */
#define AUDIO_REC_RING_SIZE  512U
#define AUDIO_PLAY_RING_SIZE 512U

typedef enum {
    AUDIO_MODE_SYNTH = 0,      /* mic -> voice filter -> headphone out, live */
    AUDIO_MODE_SD_PLAYBACK = 1 /* headphone out driven by the playback ring buffer (fed from an SD .wav file) */
} AudioMode_t;

typedef enum {
    VOICE_FILTER_OFF = 0,
    VOICE_FILTER_LOWPASS,
    VOICE_FILTER_HIGHPASS,
    VOICE_FILTER_ROBOT,
    VOICE_FILTER_COUNT
} VoiceFilter_t;

/* Real FFT spectrum (CMSIS-DSP), refreshed roughly every FFT_SIZE samples
 * (~16ms at 16kHz). Magnitude, log-compressed to 0-255 for easy bar drawing. */
extern volatile bool AudioEngine_FFTReady;
extern uint8_t AudioEngine_FFTBins[FFT_SIZE / 2];

void AudioEngine_Init(void);
void AudioEngine_Process(void); /* call every iteration of the main superloop */
void AudioEngine_SetMode(AudioMode_t mode);

void AudioEngine_SetVoiceFilter(VoiceFilter_t filter);
VoiceFilter_t AudioEngine_GetVoiceFilter(void);
const char *AudioEngine_GetVoiceFilterName(VoiceFilter_t filter);

/* --- Recording: mic samples pushed here by AudioEngine_Process(), drained
 * by sdcard_wav.c into an open .wav file. --- */
void AudioEngine_SetRecordEnable(bool enable);
bool AudioEngine_IsRecording(void);
uint16_t AudioEngine_RecordAvailable(void);
uint16_t AudioEngine_RecordRead(int16_t *dst, uint16_t maxSamples);

/* --- Playback: sdcard_wav.c pushes decoded samples here, AudioEngine_Process()
 * drains them out to the headphone modulator when in AUDIO_MODE_SD_PLAYBACK. --- */
void AudioEngine_SetPlaybackEnable(bool enable);
bool AudioEngine_IsPlaying(void);
uint16_t AudioEngine_PlaybackFree(void);
uint16_t AudioEngine_PlaybackWrite(const int16_t *src, uint16_t n);

/* Smoothed live input level (0-32767), for UI meters and the WiFi status page. */
uint16_t AudioEngine_GetLevel(void);

/* Free-running uptime, derived from the output-modulator SysTick -- used for
 * WiFi AT-command timeouts and the WiFi status page. */
uint32_t AudioEngine_GetUptimeMs(void);

#endif
