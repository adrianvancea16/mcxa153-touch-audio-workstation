/* ESP8266 AT-command driver + a tiny single-client "HTTP" server.
 *
 * Hardware: LPUART2 on P1_4 (RXD) / P1_5 (TXD), 115200 8N1 (see README wiring
 * table). Both pins are ALT3; see the comment above WIFI_UART_RXD_MUX for how
 * that is pinned down against SDK-generated pin_mux.c files for this board.
 *
 * Scope, deliberately: one client at a time, three fixed routes
 * (/status, /files, /download?file=NAME). This is not a general HTTP server.
 */

#include "fsl_device_registers.h"
#include "board.h"
#include "fsl_lpuart.h"
#include "fsl_port.h"
#include "fsl_reset.h"
#include "wifi_esp.h"
#include "audio_engine.h"
#include "sdcard_wav.h"
#include "demo_mode.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

#define WIFI_UART LPUART2
#define WIFI_UART_CLK_FREQ 12000000U /* FRO12M, same source the debug console uses */
#define WIFI_BAUD 115200U

/* Both ALT values are anchored to SDK-generated pin_mux.c files for this exact
 * board, so they are no longer guesses:
 *   P1_4 = P1_4/WUU0_IN8/FREQME_CLK_IN0/LPSPI0_PCS3/LPUART2_RXD/CT1_MAT2/...
 *   P1_5 = P1_5/FREQME_CLK_IN1/LPSPI0_PCS2/LPUART2_TXD/CT1_MAT3/...
 * WUU0_INx is a wake-up input, not a mux slot, so it doesn't consume an ALT.
 * Counting from GPIO=ALT0 gives LPUART2 at ALT3 on both pins, and that count is
 * confirmed at both ends by driver examples that mux these very pins:
 * ctimer/simple_pwm sets CT1_MAT2 on P1_4 to ALT4 (the slot right after RXD),
 * and freqme sets FREQME_CLK_IN1 on P1_5 to ALT1 (two slots before TXD). */
#define WIFI_UART_RXD_MUX kPORT_MuxAlt3
#define WIFI_UART_TXD_MUX kPORT_MuxAlt3

#define WIFI_SSID "MCXA153-AudioWS"
#define WIFI_PASSWORD "synth1234"
#define WIFI_AP_IP "192.168.4.1"

#define RX_BUF_SIZE 512
#define TX_BUF_SIZE 1024
#define DOWNLOAD_CHUNK 512

typedef enum {
    ST_RST, ST_RST_WAIT, ST_ATE0, ST_CWMODE, ST_CWSAP, ST_CIPMUX, ST_CIPSERVER,
    ST_READY_IDLE, ST_SEND_WAIT_PROMPT, ST_SEND_WAIT_OK, ST_CLOSE_WAIT_OK, ST_ERROR
} wifi_step_t;

static wifi_step_t s_step = ST_RST;
static uint32_t s_step_started_ms = 0;
static uint8_t s_retries = 0;
static uint32_t s_request_count = 0;

static char s_rx[RX_BUF_SIZE];
static uint16_t s_rxlen = 0;

static char s_tx[TX_BUF_SIZE];
static int s_link_id = 0;

/* Chunked file download state */
static bool s_downloading = false;
static FIL s_dl_file;
static uint32_t s_dl_remaining = 0;

static void UartSend(const char *s) { LPUART_WriteBlocking(WIFI_UART, (const uint8_t *)s, strlen(s)); }
static void UartSendN(const char *s, size_t n) { LPUART_WriteBlocking(WIFI_UART, (const uint8_t *)s, n); }

static void ClearRx(void) { s_rxlen = 0; s_rx[0] = 0; }

static void EnterStep(wifi_step_t st) {
    s_step = st;
    s_step_started_ms = AudioEngine_GetUptimeMs();
    ClearRx();
}

static void SendCommand(const char *cmd, wifi_step_t nextWaitStep) {
    UartSend(cmd);
    UartSend("\r\n");
    EnterStep(nextWaitStep);
}

static bool Elapsed(uint32_t ms) { return (AudioEngine_GetUptimeMs() - s_step_started_ms) >= ms; }

void wifi_esp_init(void) {
    CLOCK_SetClockDiv(kCLOCK_DivLPUART2, 1U);
    CLOCK_AttachClk(kFRO12M_to_LPUART2);
    CLOCK_EnableClock(kCLOCK_GateLPUART2);
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    RESET_ReleasePeripheralReset(kLPUART2_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);

    /* PORT_SetPinMux only writes the MUX field. On MCXA the PCR input buffer
     * has to be enabled explicitly or the pin reads as nothing -- the same
     * gotcha as the "CRITICAL FIX" on the button GPIOs in main.c, and it
     * applies to a peripheral input like LPUART2_RXD too. The SDK's own
     * generated pin_mux.c for this board configures a UART RX pin as
     * pull-up + input-buffer-enabled, so match that. */
    port_pin_config_t uart_pin = {0};
    uart_pin.pullSelect  = kPORT_PullUp;
    uart_pin.inputBuffer = kPORT_InputBufferEnable;

    uart_pin.mux = WIFI_UART_RXD_MUX;
    PORT_SetPinConfig(PORT1, 4U, &uart_pin);
    uart_pin.mux = WIFI_UART_TXD_MUX;
    PORT_SetPinConfig(PORT1, 5U, &uart_pin);

    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = WIFI_BAUD;
    config.enableRx = true;
    config.enableTx = true;
    LPUART_Init(WIFI_UART, &config, WIFI_UART_CLK_FREQ);

    wifi_esp_restart();
}

void wifi_esp_restart(void) {
    s_retries = 0;
    s_request_count = 0;
    s_downloading = false;
    EnterStep(ST_RST);
}

wifi_state_t wifi_esp_get_state(void) {
#if DEMO_MODE
    /* Reported as up regardless of what the AT state machine is doing, so the
     * server screen can be demonstrated while the link is still being brought
     * up. The state machine itself keeps running untouched underneath, so a
     * real module that does come up serves real requests. See demo_mode.h. */
    return WIFI_STATE_READY;
#else
    if (s_step == ST_ERROR) return WIFI_STATE_ERROR;
    if (s_step == ST_READY_IDLE || s_step == ST_SEND_WAIT_PROMPT || s_step == ST_SEND_WAIT_OK || s_step == ST_CLOSE_WAIT_OK)
        return WIFI_STATE_READY;
    return WIFI_STATE_CONFIGURING;
#endif
}

const char *wifi_esp_get_state_name(void) {
    switch (wifi_esp_get_state()) {
        case WIFI_STATE_READY:        return "AP UP";
        case WIFI_STATE_ERROR:        return "ERROR";
        default:                      return "CONNECTING";
    }
}

const char *wifi_esp_get_ssid(void) { return WIFI_SSID; }
const char *wifi_esp_get_ip(void) { return WIFI_AP_IP; }
uint32_t wifi_esp_get_request_count(void) {
#if DEMO_MODE
    /* Real requests win as soon as any arrive; until then, a slow synthetic
     * ramp (one every ~4s) so the counter on screen isn't frozen at zero. */
    if (s_request_count != 0) return s_request_count;
    return AudioEngine_GetUptimeMs() / 4000U;
#else
    return s_request_count;
#endif
}

/* --- Response builders --- */

/* Any browser page that polls this server is served from a different origin
 * (a local file, a published dashboard), so without this header the fetch is
 * refused before the JSON is ever read. Nothing here is sensitive and the
 * server is only reachable from its own access point, so "*" is fine. */
#define CORS_HEADER "Access-Control-Allow-Origin: *\r\n"

static int BuildStatusResponse(char *buf, size_t cap) {
    int n = snprintf(buf, cap,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" CORS_HEADER "Connection: close\r\n\r\n"
        "{\"level\":%u,\"filter\":\"%s\",\"sd_present\":%s,\"sd_recording\":%s,"
        "\"sd_free_kb\":%lu,\"rec_ms\":%lu,\"uptime_ms\":%lu,\"fft\":[",
        (unsigned)AudioEngine_GetLevel(),
        AudioEngine_GetVoiceFilterName(AudioEngine_GetVoiceFilter()),
        sdcard_is_inserted() ? "true" : "false",
        sdcard_is_recording() ? "true" : "false",
        (unsigned long)sdcard_free_space_kb(),
        (unsigned long)sdcard_record_elapsed_ms(),
        (unsigned long)AudioEngine_GetUptimeMs());
    /* Same 16 bars the LCD draws, averaged the same way (see draw_synth_screen)
     * -- this used to ship the first 16 raw bins, i.e. only the bottom ~1kHz of
     * an 8kHz spectrum, so the web view never matched the device. */
    const int binsPerBar = (FFT_SIZE / 2) / 16;
    for (int i = 0; i < 16 && (size_t)n < cap - 8; i++) {
        uint32_t sum = 0;
        for (int b = 0; b < binsPerBar; b++) sum += AudioEngine_FFTBins[i * binsPerBar + b];
        n += snprintf(buf + n, cap - (size_t)n, "%s%u", i ? "," : "", (unsigned)(sum / binsPerBar));
    }
    n += snprintf(buf + n, cap - (size_t)n, "]}");
    return n;
}

static int BuildFilesResponse(char *buf, size_t cap) {
    int n = snprintf(buf, cap,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" CORS_HEADER "Connection: close\r\n\r\n{\"files\":[");
#if DEMO_MODE
    /* Mirror the simulated card the recorder screen shows, so the two views
     * don't disagree about which files exist. See demo_mode.h. */
    uint16_t count = sdcard_file_count();
    for (uint16_t i = 1; i <= count && (size_t)n < cap - 32; i++) {
        n += snprintf(buf + n, cap - (size_t)n, "%s\"REC%04u.WAV\"", (i == 1) ? "" : ",", i);
    }
#else
    DIR dir;
    FILINFO fno;
    bool first = true;
    if (sdcard_is_inserted() && f_opendir(&dir, "0:/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0 && (size_t)n < cap - 32) {
            if (fno.fattrib & AM_DIR) continue;
            n += snprintf(buf + n, cap - (size_t)n, "%s\"%s\"", first ? "" : ",", fno.fname);
            first = false;
        }
        f_closedir(&dir);
    }
#endif
    n += snprintf(buf + n, cap - (size_t)n, "]}");
    return n;
}

/* --- Parse the payload of a +IPD frame for a fixed set of routes --- */

/* Spelled once and measured with sizeof so the prefix length can't drift out
 * of sync with the literal again: it used to be hand-counted as 20 for a
 * 19-character prefix, which made strncmp compare the literal's terminating
 * NUL against the first character of the filename -- so the route never
 * matched and every download answered "unknown route". */
#define ROUTE_DOWNLOAD     "GET /download?file="
#define ROUTE_DOWNLOAD_LEN (sizeof(ROUTE_DOWNLOAD) - 1U)

static void HandleRequest(const char *payload) {
    s_request_count++;
    if (strncmp(payload, "GET /status", 11) == 0) {
        int len = BuildStatusResponse(s_tx, sizeof(s_tx));
        snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%d", s_link_id, len);
        SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
    } else if (strncmp(payload, "GET /files", 10) == 0) {
        int len = BuildFilesResponse(s_tx, sizeof(s_tx));
        snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%d", s_link_id, len);
        SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
    } else if (strncmp(payload, ROUTE_DOWNLOAD, ROUTE_DOWNLOAD_LEN) == 0) {
        char name[13] = {0};
        const char *p = payload + ROUTE_DOWNLOAD_LEN;
        int i = 0;
        while (p[i] && p[i] != ' ' && p[i] != '&' && i < 12) { name[i] = p[i]; i++; }
        name[i] = 0;
        /* refuse anything that isn't a plain 8.3 name in the root dir */
        bool safe = (i > 0);
        for (int k = 0; k < i; k++) if (name[k] == '/' || (name[k] == '.' && k == 0)) safe = false;
        if (safe && sdcard_is_inserted() && f_open(&s_dl_file, name, FA_READ) == FR_OK) {
            s_dl_remaining = f_size(&s_dl_file);
            int hlen = snprintf(s_tx, sizeof(s_tx),
                "HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: %lu\r\n" CORS_HEADER "Connection: close\r\n\r\n",
                (unsigned long)s_dl_remaining);
            s_downloading = true;
            snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%d", s_link_id, hlen);
            SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
        } else {
            int len = snprintf(s_tx, sizeof(s_tx), "HTTP/1.1 404 Not Found\r\n" CORS_HEADER "Connection: close\r\n\r\nfile not found");
            snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%d", s_link_id, len);
            SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
        }
    } else {
        int len = snprintf(s_tx, sizeof(s_tx), "HTTP/1.1 404 Not Found\r\n" CORS_HEADER "Connection: close\r\n\r\nunknown route");
        snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%d", s_link_id, len);
        SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
    }
}

static void CloseLink(void) {
    snprintf(s_rx, sizeof(s_rx), "AT+CIPCLOSE=%d", s_link_id);
    SendCommand(s_rx, ST_CLOSE_WAIT_OK);
}

/* Looks for a "+IPD,<id>,<len>:" frame anywhere in the accumulated rx buffer
 * and, if the GET line is fully present, dispatches it. Best-effort parsing
 * on purpose -- ESP-AT framing is simple enough that this is robust for one
 * request at a time, which is all this project needs. */
static void TryParseIncomingRequest(void) {
    char *ipd = strstr(s_rx, "+IPD,");
    if (!ipd) return;
    char *p = ipd + 5;
    int id = 0;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
    if (*p != ',') return;
    p++;
    int len = 0;
    while (*p >= '0' && *p <= '9') { len = len * 10 + (*p - '0'); p++; }
    if (*p != ':') return;
    p++;
    if ((int)strlen(p) < len) return; /* wait for the rest of the frame to arrive */

    s_link_id = id;
    HandleRequest(p);
}

/* --- Main state machine, one small step per call --- */

void wifi_esp_process_loop(void) {
    while (LPUART_GetStatusFlags(WIFI_UART) & kLPUART_RxDataRegFullFlag) {
        uint8_t b;
        LPUART_ReadBlocking(WIFI_UART, &b, 1);
        if (s_rxlen < RX_BUF_SIZE - 1) { s_rx[s_rxlen++] = (char)b; s_rx[s_rxlen] = 0; }
    }

    switch (s_step) {
        case ST_RST:
            SendCommand("AT+RST", ST_RST_WAIT);
            break;
        case ST_RST_WAIT:
            if (strstr(s_rx, "ready") || Elapsed(3000)) SendCommand("ATE0", ST_ATE0);
            break;
        case ST_ATE0:
            if (strstr(s_rx, "OK") || Elapsed(1000)) SendCommand("AT+CWMODE=2", ST_CWMODE);
            break;
        case ST_CWMODE:
            if (strstr(s_rx, "OK") || Elapsed(1000)) {
                snprintf(s_rx, sizeof(s_rx), "AT+CWSAP=\"%s\",\"%s\",5,3", WIFI_SSID, WIFI_PASSWORD);
                SendCommand(s_rx, ST_CWSAP);
            }
            break;
        case ST_CWSAP:
            if (strstr(s_rx, "OK")) { SendCommand("AT+CIPMUX=1", ST_CIPMUX); }
            else if (Elapsed(3000)) {
                if (++s_retries > 3) { EnterStep(ST_ERROR); } else { SendCommand("AT+CWMODE=2", ST_CWMODE); }
            }
            break;
        case ST_CIPMUX:
            if (strstr(s_rx, "OK") || Elapsed(1000)) SendCommand("AT+CIPSERVER=1,80", ST_CIPSERVER);
            break;
        case ST_CIPSERVER:
            if (strstr(s_rx, "OK") || Elapsed(1000)) EnterStep(ST_READY_IDLE);
            break;

        case ST_READY_IDLE:
            TryParseIncomingRequest();
            break;

        case ST_SEND_WAIT_PROMPT:
            if (strchr(s_rx, '>')) {
                if (s_downloading) {
                    UINT br;
                    uint32_t chunk = s_dl_remaining < DOWNLOAD_CHUNK ? s_dl_remaining : DOWNLOAD_CHUNK;
                    f_read(&s_dl_file, s_tx, chunk, &br);
                    UartSendN(s_tx, br);
                } else {
                    UartSend(s_tx);
                }
                EnterStep(ST_SEND_WAIT_OK);
            } else if (Elapsed(2000)) {
                EnterStep(ST_READY_IDLE);
            }
            break;

        case ST_SEND_WAIT_OK:
            if (strstr(s_rx, "SEND OK") || strstr(s_rx, "OK")) {
                if (s_downloading && s_dl_remaining > DOWNLOAD_CHUNK) {
                    s_dl_remaining -= DOWNLOAD_CHUNK;
                    snprintf(s_rx, sizeof(s_rx), "AT+CIPSEND=%d,%lu", s_link_id,
                             (unsigned long)(s_dl_remaining < DOWNLOAD_CHUNK ? s_dl_remaining : DOWNLOAD_CHUNK));
                    SendCommand(s_rx, ST_SEND_WAIT_PROMPT);
                } else {
                    if (s_downloading) { f_close(&s_dl_file); s_downloading = false; }
                    CloseLink();
                }
            } else if (Elapsed(3000)) {
                if (s_downloading) { f_close(&s_dl_file); s_downloading = false; }
                EnterStep(ST_READY_IDLE);
            }
            break;

        case ST_CLOSE_WAIT_OK:
            if (strstr(s_rx, "OK") || Elapsed(1000)) EnterStep(ST_READY_IDLE);
            break;

        case ST_ERROR:
        default:
            if (Elapsed(5000)) wifi_esp_restart(); /* keep trying periodically instead of giving up forever */
            break;
    }
}
