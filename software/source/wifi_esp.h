#ifndef WIFI_ESP_H
#define WIFI_ESP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WIFI_STATE_CONFIGURING = 0, /* bringing the AT link / access point up */
    WIFI_STATE_READY,           /* AP + TCP server on :80 are up */
    WIFI_STATE_ERROR            /* a setup command failed or timed out repeatedly */
} wifi_state_t;

/* Configures LPUART2 (P1_4=RXD, P1_5=TXD, 115200 8N1) -- does not block waiting
 * for the module; the AT bring-up sequence runs from wifi_esp_process_loop(). */
void wifi_esp_init(void);

/* Call every iteration of the main superloop. Non-blocking: advances the AT
 * state machine by at most a little work per call. */
void wifi_esp_process_loop(void);

/* Re-runs the whole AT bring-up sequence (SW2 in WIFI SERVER mode). */
void wifi_esp_restart(void);

wifi_state_t wifi_esp_get_state(void);
const char *wifi_esp_get_state_name(void);
const char *wifi_esp_get_ssid(void);
const char *wifi_esp_get_ip(void);
uint32_t wifi_esp_get_request_count(void);

#endif /* WIFI_ESP_H */
