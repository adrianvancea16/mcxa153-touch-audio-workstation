#include "fsl_device_registers.h"
#include "board.h"
#include "fsl_lpadc.h"
#include "audio_engine.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "core_cm33.h"
#include <math.h>

#define BOARD_MIC_ADC_BASE ADC0
#define BOARD_MIC_ADC_CHANNEL 8U

/* A single SysTick drives both the output modulator (every tick) and the
 * 16kHz sample-rate trigger (every Nth tick) -- same architecture as the
 * original, proven-on-hardware code, just at a higher base rate for a
 * cleaner noise-shaped output. A separate CTIMER0-based sample timer was
 * tried here and pulled back out: it was unverified on real hardware and,
 * if its interrupt doesn't fire the way expected, the whole audio path goes
 * silent (s_current_sample_u16 never updates) -- exactly the "I don't hear
 * anything" symptom. SysTick is the one timing mechanism already known to
 * work on this board. */
/* Higher than the original's 64kHz: the 1st-order accumulator modulator
 * below is unconditionally stable at any rate (unlike the 2nd-order design
 * that was tried and reverted), so raising this just pushes its switching
 * noise further above the audible band with no added risk -- the external
 * RC filter has that much more room to attenuate it before it reaches your
 * ears. Pushed from 384kHz to 960kHz (60x oversampling instead of 24x) after
 * the "piuit" (carrier tone, not noise) turned out to persist even with the
 * mic disconnected -- consistent with the RC filter not having enough
 * separation from the carrier to knock it down far enough. This costs a bit
 * more CPU time in the ISR (~19% at 96MHz) but there's plenty of headroom.
 * Worth raising further still (clean divisors of 96MHz) if it's still audible --
 * but there is a real ceiling here: this chip has no actual audio DAC, so at
 * some point this is the practical limit of a GPIO + RC-filter "DAC". */
#define OUTPUT_MODULATOR_RATE_HZ 960000U
#define SAMPLE_TICK_DIVIDER (OUTPUT_MODULATOR_RATE_HZ / AUDIO_SAMPLE_RATE_HZ) /* 960000/16000 = 60 */

static const float PI_F = 3.14159265358979f;

/* ---------------- Generic biquad (Direct Form II Transposed) ---------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} Biquad_t;

static void Biquad_Reset(Biquad_t *f) { f->z1 = 0.0f; f->z2 = 0.0f; }

static void Biquad_SetLowpass(Biquad_t *f, float fc, float fs, float q) {
    float w0 = 2.0f * PI_F * fc / fs;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw0 = cosf(w0);
    float a0 = 1.0f + alpha;
    f->b0 = ((1.0f - cosw0) / 2.0f) / a0;
    f->b1 = (1.0f - cosw0) / a0;
    f->b2 = f->b0;
    f->a1 = (-2.0f * cosw0) / a0;
    f->a2 = (1.0f - alpha) / a0;
    Biquad_Reset(f);
}

static void Biquad_SetHighpass(Biquad_t *f, float fc, float fs, float q) {
    float w0 = 2.0f * PI_F * fc / fs;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw0 = cosf(w0);
    float a0 = 1.0f + alpha;
    f->b0 = ((1.0f + cosw0) / 2.0f) / a0;
    f->b1 = (-(1.0f + cosw0)) / a0;
    f->b2 = f->b0;
    f->a1 = (-2.0f * cosw0) / a0;
    f->a2 = (1.0f - alpha) / a0;
    Biquad_Reset(f);
}

static inline int16_t Biquad_Process(Biquad_t *f, int16_t in) {
    float x = (float)in;
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    if (y > 32767.0f) y = 32767.0f;
    if (y < -32768.0f) y = -32768.0f;
    return (int16_t)y;
}

/* Voice filter presets (Milestone 3), applied only to the SYNTHESIZER
 * headphone-output path, after the fixed-gain stage. */
static Biquad_t s_voice_lowpass;  /* "warm" muffled voice */
static Biquad_t s_voice_highpass; /* "telephone" voice */
static volatile VoiceFilter_t s_voice_filter = VOICE_FILTER_OFF;

static uint32_t s_robot_phase = 0;
#define ROBOT_CARRIER_HZ 60U
#define ROBOT_PHASE_STEP ((uint32_t)(((uint64_t)ROBOT_CARRIER_HZ << 32) / AUDIO_SAMPLE_RATE_HZ))

static int16_t ApplyRobotEffect(int16_t in) {
    s_robot_phase += ROBOT_PHASE_STEP;
    int32_t carrier = (s_robot_phase & 0x80000000U) ? 1 : -1; /* bipolar square-wave carrier -> ring modulation */
    int32_t out = (int32_t)in * carrier;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

static int16_t ApplyVoiceFilter(int16_t in) {
    switch (s_voice_filter) {
        case VOICE_FILTER_LOWPASS:  return Biquad_Process(&s_voice_lowpass, in);
        case VOICE_FILTER_HIGHPASS: return Biquad_Process(&s_voice_highpass, in);
        case VOICE_FILTER_ROBOT:    return ApplyRobotEffect(in);
        default:                    return in;
    }
}

void AudioEngine_SetVoiceFilter(VoiceFilter_t filter) {
    if (filter < VOICE_FILTER_COUNT) s_voice_filter = filter;
}
VoiceFilter_t AudioEngine_GetVoiceFilter(void) { return s_voice_filter; }

const char *AudioEngine_GetVoiceFilterName(VoiceFilter_t filter) {
    switch (filter) {
        case VOICE_FILTER_OFF:      return "OFF";
        case VOICE_FILTER_LOWPASS:  return "WARM";
        case VOICE_FILTER_HIGHPASS: return "PHONE";
        case VOICE_FILTER_ROBOT:    return "ROBOT";
        default:                    return "?";
    }
}

/* ---------------- DC blocker (unchanged idea, single-pole IIR) ---------------- */
static int16_t s_dc_x1 = 0;
static int32_t s_dc_y1 = 0;

static inline int16_t ProcessDCBlocker(int16_t x0) {
    int32_t y0 = (int32_t)x0 - (int32_t)s_dc_x1 + s_dc_y1 - (s_dc_y1 >> 8);
    s_dc_x1 = x0;
    s_dc_y1 = y0;
    int32_t out = y0;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

/* AGC and the anti-alias biquad (both introduced by me, neither verified on
 * real hardware) are gone from the live signal path as of this revision --
 * several rounds of retuning never fixed the actual problem on the real
 * board, only the original fixed-4x-gain path is confirmed to have worked.
 * ProcessOneSample() below now matches that original path exactly. */

/* ---------------- Live level (smoothed, for UI meters / WiFi status) ---------------- */
static float s_level_smooth = 0.0f;
uint16_t AudioEngine_GetLevel(void) { return (uint16_t)s_level_smooth; }

/* ---------------- Real FFT (self-contained iterative radix-2, no external DSP lib) ----------------
 * 256-point, applied to the "clean" (anti-aliased + AGC'd) signal, Hann-windowed
 * to reduce spectral leakage. Cheap enough on a 96MHz Cortex-M33 to run once
 * every 256 samples (~16ms) with room to spare. */
static float s_fft_re[FFT_SIZE];
static float s_fft_im[FFT_SIZE];
static float s_hann[FFT_SIZE];
static float s_twiddle_re[FFT_SIZE / 2];
static float s_twiddle_im[FFT_SIZE / 2];
static uint16_t s_fft_idx = 0;

volatile bool AudioEngine_FFTReady = false;
uint8_t AudioEngine_FFTBins[FFT_SIZE / 2] = {0};

static void FFT_InitTables(void) {
    for (int n = 0; n < FFT_SIZE; n++) {
        s_hann[n] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)n / (float)(FFT_SIZE - 1));
    }
    for (int k = 0; k < FFT_SIZE / 2; k++) {
        float angle = -2.0f * PI_F * (float)k / (float)FFT_SIZE;
        s_twiddle_re[k] = cosf(angle);
        s_twiddle_im[k] = sinf(angle);
    }
}

static uint8_t BitReverse8(uint8_t x) {
    x = (uint8_t)((x >> 4) | (x << 4));
    x = (uint8_t)(((x & 0xCCU) >> 2) | ((x & 0x33U) << 2));
    x = (uint8_t)(((x & 0xAAU) >> 1) | ((x & 0x55U) << 1));
    return x;
}

static void FFT_Compute(void) {
    for (int i = 0; i < FFT_SIZE; i++) {
        int j = BitReverse8((uint8_t)i);
        if (j > i) {
            float tr = s_fft_re[i]; s_fft_re[i] = s_fft_re[j]; s_fft_re[j] = tr;
            float ti = s_fft_im[i]; s_fft_im[i] = s_fft_im[j]; s_fft_im[j] = ti;
        }
    }
    for (int size = 2; size <= FFT_SIZE; size <<= 1) {
        int halfsize = size / 2;
        int tablestep = FFT_SIZE / size;
        for (int i = 0; i < FFT_SIZE; i += size) {
            int k = 0;
            for (int j = i; j < i + halfsize; j++, k += tablestep) {
                int idx2 = j + halfsize;
                float tre = s_fft_re[idx2] * s_twiddle_re[k] - s_fft_im[idx2] * s_twiddle_im[k];
                float tim = s_fft_re[idx2] * s_twiddle_im[k] + s_fft_im[idx2] * s_twiddle_re[k];
                s_fft_re[idx2] = s_fft_re[j] - tre;
                s_fft_im[idx2] = s_fft_im[j] - tim;
                s_fft_re[j] += tre;
                s_fft_im[j] += tim;
            }
        }
    }
    /* Real dB (log) scale, not linear -- a raw FFT magnitude for a 256-point
     * transform of a normal-volume voice signal is naturally in the tens of
     * thousands to over a million (it scales with N * amplitude, not with
     * amplitude alone), while a fixed linear divisor has no way to stay
     * sensible across that range: it either pins every bar to max for any
     * real voice ("always red/maxed", the actual bug here) or shows nothing
     * for a quiet one. Every real spectrum analyzer uses dB for this reason. */
    #define FFT_DB_FLOOR 20.0f  /* maps to bar height 0 -- raise this if bars sit lit even in silence */
    #define FFT_DB_CEIL  100.0f /* maps to bar height 255 -- lower this if bars never reach the top on loud speech */
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float mag = sqrtf(s_fft_re[i] * s_fft_re[i] + s_fft_im[i] * s_fft_im[i]);
        float db = 20.0f * log10f(mag + 1.0f);
        float norm = (db - FFT_DB_FLOOR) / (FFT_DB_CEIL - FFT_DB_FLOOR);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        AudioEngine_FFTBins[i] = (uint8_t)(norm * 255.0f);
    }
}

/* ---------------- Streaming record/playback ring buffers ---------------- *
 * Now that per-sample processing runs inside SysTick_Handler (see below) to
 * keep audio timing immune to the LCD's slow bit-banged drawing, these
 * buffers are genuinely shared between an ISR and the main loop. Rather than
 * disabling interrupts around every access, each ring uses the classic
 * lock-free single-producer/single-consumer pattern: `head` has exactly one
 * writer, `tail` has exactly one (different) writer, and each side only ever
 * *reads* the other's index. Both indices free-run (never masked directly)
 * and are only masked when used to address the array, so head-tail keeps
 * giving the right count across wraparound -- this requires the ring sizes
 * to stay powers of two. */
#if (AUDIO_REC_RING_SIZE & (AUDIO_REC_RING_SIZE - 1)) != 0
#error "AUDIO_REC_RING_SIZE must be a power of two"
#endif
#if (AUDIO_PLAY_RING_SIZE & (AUDIO_PLAY_RING_SIZE - 1)) != 0
#error "AUDIO_PLAY_RING_SIZE must be a power of two"
#endif

static int16_t s_rec_ring[AUDIO_REC_RING_SIZE];
static volatile uint16_t s_rec_head = 0; /* producer: SysTick_Handler only */
static volatile uint16_t s_rec_tail = 0; /* consumer: main loop only */
static volatile bool s_recording_enabled = false;

static int16_t s_play_ring[AUDIO_PLAY_RING_SIZE];
static volatile uint16_t s_play_head = 0; /* producer: main loop only */
static volatile uint16_t s_play_tail = 0; /* consumer: SysTick_Handler only */
static volatile bool s_playback_enabled = false;

/* Called only from SysTick_Handler (producer side). */
static void RecRingPush(int16_t s) {
    if (!s_recording_enabled) return;
    uint16_t used = (uint16_t)(s_rec_head - s_rec_tail);
    if (used >= AUDIO_REC_RING_SIZE) return; /* full: main loop isn't draining fast enough, drop the sample */
    s_rec_ring[s_rec_head & (AUDIO_REC_RING_SIZE - 1)] = s;
    s_rec_head++;
}

void AudioEngine_SetRecordEnable(bool enable) {
    s_recording_enabled = enable;
    if (enable) { s_rec_head = 0; s_rec_tail = 0; }
}
bool AudioEngine_IsRecording(void) { return s_recording_enabled; }
uint16_t AudioEngine_RecordAvailable(void) { return (uint16_t)(s_rec_head - s_rec_tail); }

/* Called only from the main loop (consumer side, e.g. sdcard_wav.c). */
uint16_t AudioEngine_RecordRead(int16_t *dst, uint16_t maxSamples) {
    uint16_t avail = (uint16_t)(s_rec_head - s_rec_tail);
    uint16_t n = (avail < maxSamples) ? avail : maxSamples;
    for (uint16_t i = 0; i < n; i++) {
        dst[i] = s_rec_ring[(uint16_t)(s_rec_tail + i) & (AUDIO_REC_RING_SIZE - 1)];
    }
    s_rec_tail = (uint16_t)(s_rec_tail + n);
    return n;
}

/* Called only from SysTick_Handler (consumer side). */
static bool PlayRingPop(int16_t *out) {
    if (s_play_head == s_play_tail) return false;
    *out = s_play_ring[s_play_tail & (AUDIO_PLAY_RING_SIZE - 1)];
    s_play_tail++;
    return true;
}

void AudioEngine_SetPlaybackEnable(bool enable) {
    s_playback_enabled = enable;
    if (!enable) { s_play_head = 0; s_play_tail = 0; }
}
bool AudioEngine_IsPlaying(void) { return s_playback_enabled; }
uint16_t AudioEngine_PlaybackFree(void) { return (uint16_t)(AUDIO_PLAY_RING_SIZE - (uint16_t)(s_play_head - s_play_tail)); }

/* Called only from the main loop (producer side, e.g. sdcard_wav.c). */
uint16_t AudioEngine_PlaybackWrite(const int16_t *src, uint16_t n) {
    uint16_t free_slots = (uint16_t)(AUDIO_PLAY_RING_SIZE - (uint16_t)(s_play_head - s_play_tail));
    uint16_t accepted = (free_slots < n) ? free_slots : n;
    for (uint16_t i = 0; i < accepted; i++) {
        s_play_ring[(uint16_t)(s_play_head + i) & (AUDIO_PLAY_RING_SIZE - 1)] = src[i];
    }
    s_play_head = (uint16_t)(s_play_head + accepted);
    return accepted;
}

/* ---------------- Mode ---------------- */
static volatile AudioMode_t s_audio_mode = AUDIO_MODE_SYNTH;
void AudioEngine_SetMode(AudioMode_t mode) { s_audio_mode = mode; }

/* ---------------- Output modulator + sample-rate tick (single SysTick) ----------------
 * IMPORTANT HARDWARE NOTE: the output is still a raw 1-bit bitstream out of a
 * GPIO pin (P3_12) -- the MCXA153 has no real audio DAC peripheral (verified
 * against the vendor register headers). For this to sound clean on the
 * 3.5mm jack you MUST add a simple RC low-pass filter between this pin and
 * the jack (e.g. ~1k series resistor + ~15-22nF to GND, cutoff ~10-15kHz).
 * Without it, any 1-bit output will sound harsh/buzzy no matter how good the
 * firmware is -- this is very likely the main reason it "sounds bad" today.
 *
 * IMPORTANT TIMING NOTE: the per-sample audio work (ADC read, filters, AGC,
 * ring buffers) used to be deferred to the main superloop via a flag, like
 * the original code did. That broke down once the LCD got real content to
 * draw: ui_display_process_loop() bit-bangs SPI in a tight loop and can block
 * the main loop for tens of milliseconds redrawing the spectrum, during which
 * the deferred processing never runs -- audio output freezes on a stale
 * sample, then jumps, which is heard as a periodic click roughly once per
 * spectrum redraw. So the per-sample work now runs directly inside this ISR
 * (see ProcessOneSample below), which fires on schedule no matter how busy
 * the main loop is. Only the FFT transform itself (not needed in real time,
 * and the most expensive part per frame) stays deferred to the main loop via
 * s_fft_pending. */
static volatile uint16_t s_current_sample_u16 = 32768;
static uint32_t s_pdm_accumulator = 0;
static volatile uint32_t s_systick_count = 0;
static uint8_t s_sample_tick_div = 0;
static volatile bool s_fft_pending = false;

static void ProcessOneSample(void) {
    static uint16_t raw_adc = 32768;
    lpadc_conv_result_t adc_res;
    if (LPADC_GetConvResult(BOARD_MIC_ADC_BASE, &adc_res)) {
        raw_adc = adc_res.convValue;
    }
    LPADC_DoSoftwareTrigger(BOARD_MIC_ADC_BASE, 1U);

    int16_t mic_signed = (int16_t)raw_adc - 32768;
    mic_signed = ProcessDCBlocker(mic_signed);

    /* Back to the exact gain stage from the original, working-on-hardware
     * code: fixed 4x with a hard clip. The anti-alias filter and AGC were
     * both untested software additions of mine and, despite several rounds
     * of tuning, never actually fixed anything on real hardware -- only this
     * fixed-gain version is confirmed to have worked. */
    int32_t gained = (int32_t)mic_signed * 4;
    if (gained > 32767) gained = 32767;
    if (gained < -32768) gained = -32768;
    mic_signed = (int16_t)gained;

    float abslevel = (float)((mic_signed < 0) ? -mic_signed : mic_signed);
    s_level_smooth += (abslevel - s_level_smooth) * 0.05f;

    RecRingPush(mic_signed);

    s_fft_re[s_fft_idx] = (float)mic_signed * s_hann[s_fft_idx];
    s_fft_im[s_fft_idx] = 0.0f;
    s_fft_idx++;
    if (s_fft_idx >= FFT_SIZE) {
        s_fft_idx = 0;
        s_fft_pending = true; /* actual transform happens in AudioEngine_Process(), off the real-time path */
    }

    int16_t out_sample;
    if (s_audio_mode == AUDIO_MODE_SYNTH) {
        out_sample = ApplyVoiceFilter(mic_signed);
    } else {
        int16_t p;
        out_sample = (s_playback_enabled && PlayRingPop(&p)) ? p : 0;
    }

    s_current_sample_u16 = (uint16_t)((int32_t)out_sample + 32768);
}

void SysTick_Handler(void) {
    /* Output modulator: back to the original 1st-order accumulator (simple
     * PDM/pulse-density, unconditionally stable at any rate). Had replaced
     * this with a 2nd-order error-feedback noise shaper for better theoretical
     * SNR, but at only 12x oversampling that's a genuinely low ratio for a
     * 2nd-order design -- exactly the kind of setup where such modulators are
     * prone to limit cycles / idle tones, i.e. a loud, input-independent
     * "whoosh" -- which matches what showed up on real hardware. The simple
     * accumulator has no such stability concern. */
    s_pdm_accumulator += s_current_sample_u16;
    if (s_pdm_accumulator >= 65536) {
        s_pdm_accumulator -= 65536;
        GPIO3->PSOR = (1U << 12);
    } else {
        GPIO3->PCOR = (1U << 12);
    }
    s_systick_count++;

    /* Sample-rate tick: every SAMPLE_TICK_DIVIDER-th tick (16kHz), do the
     * actual per-sample audio work right here (see note above). */
    s_sample_tick_div++;
    if (s_sample_tick_div >= SAMPLE_TICK_DIVIDER) {
        s_sample_tick_div = 0;
        ProcessOneSample();
    }
}

uint32_t AudioEngine_GetUptimeMs(void) {
    return s_systick_count / (OUTPUT_MODULATOR_RATE_HZ / 1000U);
}

/* ---------------- Non-real-time work (called from the main superloop) ---------------- */
void AudioEngine_Process(void) {
    if (!s_fft_pending) return;
    s_fft_pending = false;
    FFT_Compute();
    AudioEngine_FFTReady = true;
}

void AudioEngine_Init(void) {
    CLOCK_SetClockDiv(kCLOCK_DivADC0, 1U);
    CLOCK_AttachClk(kFRO_HF_to_ADC0);
    CLOCK_EnableClock(kCLOCK_GateADC0);
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    CLOCK_EnableClock(kCLOCK_GateGPIO1);
    CLOCK_EnableClock(kCLOCK_GatePORT3);
    CLOCK_EnableClock(kCLOCK_GateGPIO3);

    RESET_ReleasePeripheralReset(kADC0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);

    /* Audio OUT pin (noise-shaped 1-bit bitstream on GPIO -- see hardware note above) */
    PORT_SetPinMux(PORT3, 12U, kPORT_MuxAlt0);
    gpio_pin_config_t out_cfg = {kGPIO_DigitalOutput, 0};
    GPIO_PinInit(GPIO3, 12U, &out_cfg);

    /* Audio IN pin (mic, via LPADC0 channel 8) */
    PORT_SetPinMux(PORT1, 10U, 0U);

    lpadc_config_t lpadcConfig;
    LPADC_GetDefaultConfig(&lpadcConfig);
    lpadcConfig.enableAnalogPreliminary = true;
    LPADC_Init(BOARD_MIC_ADC_BASE, &lpadcConfig);

    lpadc_conv_command_config_t cmdConfig;
    LPADC_GetDefaultConvCommandConfig(&cmdConfig);
    cmdConfig.channelNumber = BOARD_MIC_ADC_CHANNEL;
    /* Tried switching this to kLPADC_ConversionResolutionHigh (16-bit) at one
     * point, reasoning that the rest of the pipeline assumes a 16-bit-scale
     * sample. That mode is unverified on this exact silicon and, on real
     * hardware, the mic stopped producing any voice at all afterwards (just
     * the modulator's constant switching hiss) -- consistent with the
     * conversion never actually completing in that mode. Reverted to the
     * SDK default (12-bit "Standard"), which is what worked (poorly, but
     * audibly) in the original code. The AGC below already adapts its gain
     * to whatever raw amplitude it actually receives, so it doesn't need the
     * ADC to be 16-bit to do its job -- it'll just run with a higher gain
     * factor to compensate for the smaller native swing. */
    LPADC_SetConvCommandConfig(BOARD_MIC_ADC_BASE, 1U, &cmdConfig);

    lpadc_conv_trigger_config_t trigConfig;
    LPADC_GetDefaultConvTriggerConfig(&trigConfig);
    trigConfig.targetCommandId = 1U;
    LPADC_SetConvTriggerConfig(BOARD_MIC_ADC_BASE, 0U, &trigConfig);

    LPADC_DoSoftwareTrigger(BOARD_MIC_ADC_BASE, 1U);

    Biquad_SetLowpass(&s_voice_lowpass, 900.0f, (float)AUDIO_SAMPLE_RATE_HZ, 0.707f);
    Biquad_SetHighpass(&s_voice_highpass, 1200.0f, (float)AUDIO_SAMPLE_RATE_HZ, 0.707f);

    FFT_InitTables();

    SysTick_Config(SystemCoreClock / OUTPUT_MODULATOR_RATE_HZ);
    NVIC_SetPriority(SysTick_IRQn, 0);
}
