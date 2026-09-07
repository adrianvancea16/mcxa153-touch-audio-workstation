#ifndef DEMO_MODE_H
#define DEMO_MODE_H

/* Presentation build.
 *
 * The SD recorder and the Wi-Fi server are still being brought up (see
 * .agents/HANDOFF.md), so with DEMO_MODE on, those two screens run off
 * simulated state instead of the real peripherals. That makes the board
 * demonstrable and photographable while the hardware work continues.
 *
 * Two rules keep this honest, and they matter because these screens end up as
 * documentation screenshots:
 *
 *   1. Nothing here touches the real drivers. diskio.c, sdcard_wav.c's file
 *      I/O and the ESP8266 AT state machine are untouched; the simulated
 *      values are substituted only at the status-query boundary that the UI
 *      reads. Turning DEMO_MODE off restores the real behaviour exactly.
 *   2. Every screen that shows simulated state says so on screen, and the
 *      HTML dashboard labels its data source too. A screenshot of this build
 *      is never meant to pass as working hardware.
 *
 * Set to 0 for a normal build. */
#ifndef DEMO_MODE
#define DEMO_MODE 1
#endif

#endif /* DEMO_MODE_H */
