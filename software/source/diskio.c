/*-----------------------------------------------------------------------/
/ Low level disk I/O module for FatFs -- SD card in SPI mode, bit-banged
/ on the same GPIO pins ui_display.c uses for the ILI9341 LCD (the LCD
/ module's SD slot shares SCK/MOSI/MISO with the display, per the project
/ wiring doc; only the chip-select line differs: SD_CS = P1_3).
/
/ This is the classic elm-chan "MMC/SDC in SPI mode" sequence (CMD0/CMD8/
/ ACMD41/CMD58 for init, CMD17/CMD24 for single-block read/write). Only
/ physical drive 0 is implemented -- this project mounts exactly one card.
/------------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_common.h"
#include "fsl_reset.h"

/* Bring-up here can only ever be debugged against a real card, so the
 * step-by-step trace stays in the source with one switch to silence it. */
#ifndef SD_DEBUG
#define SD_DEBUG 1
#endif
#if SD_DEBUG
#include <stdio.h>
#define SD_TRACE(...) printf(__VA_ARGS__)
#else
#define SD_TRACE(...) ((void)0)
#endif

/* Bus pins shared with the LCD (see ui_display.c) */
#define SPI_SCK_GPIO  GPIO2
#define SPI_SCK_PIN   12U
#define SPI_MOSI_GPIO GPIO2
#define SPI_MOSI_PIN  13U
#define SPI_MISO_GPIO GPIO2
#define SPI_MISO_PIN  16U

/* SD-only chip select: NXP P1_3 (see README wiring table) */
#define SD_CS_GPIO GPIO1
#define SD_CS_PIN  3U

#define SCK_BIT  (1U << SPI_SCK_PIN)
#define MOSI_BIT (1U << SPI_MOSI_PIN)
#define MISO_BIT (1U << SPI_MISO_PIN)
#define CS_BIT   (1U << SD_CS_PIN)

static uint8_t s_card_type = 0; /* bit0: SDv1/MMC, bit1: SDv2, bit2: block-addressed (SDHC/SDXC) */
static volatile DSTATUS s_status = STA_NOINIT;

/* Clock throttle for the identification phase.
 *
 * This bit-bang has no rate limit of its own: it toggles GPIO as fast as the
 * core can, which lands somewhere in the MHz. The ILI9341 is perfectly happy
 * there (that's why the LCD works), but the SD spec requires CMD0/CMD8/ACMD41
 * to be clocked at 100-400kHz, and plenty of cards simply do not answer above
 * that -- CMD0 then never returns 0x01 and the card reads as absent no matter
 * how it's wired. So stretch both clock half-periods during init, then drop
 * back to full speed for block transfers once the card is up.
 *
 * The per-iteration cost of the busy-wait below is a guess (a volatile
 * load/compare/increment/store/branch), deliberately on the low side so the
 * error lands on the safe side: too slow only costs a slower mount. */
#define SD_INIT_CLOCK_HZ       200000U
#define SD_DELAY_CYCLES_PER_IT 6U

/* Retry budget for the ACMD41/CMD1 "leave idle" poll. These used to be 20000,
 * which was fine when the bus ran at MHz speed; at the throttled ~200kHz one
 * pass costs roughly 1.5ms, so 20000 would stall a mount for half a minute on
 * a card that never comes up. The spec gives a card 1 second to finish
 * initialising, so ~2s of budget is generous. */
#define SD_IDLE_POLL_TRIES 1400

static uint32_t s_spi_half_delay = 0; /* 0 = full speed */

static inline void SpiHalfDelay(void) {
    for (volatile uint32_t i = 0; i < s_spi_half_delay; i++) { }
}

static inline void CS_LOW(void)  { SD_CS_GPIO->PCOR = CS_BIT; }
static inline void CS_HIGH(void) { SD_CS_GPIO->PSOR = CS_BIT; }

static inline uint8_t SD_SPI_Xfer(uint8_t out) {
    uint8_t in = 0;
    for (int b = 0; b < 8; b++) {
        SPI_SCK_GPIO->PCOR = SCK_BIT;
        if (out & 0x80) SPI_MOSI_GPIO->PSOR = MOSI_BIT;
        else            SPI_MOSI_GPIO->PCOR = MOSI_BIT;
        out = (uint8_t)(out << 1);
        SpiHalfDelay(); /* data setup, SCK low */
        SPI_SCK_GPIO->PSOR = SCK_BIT;
        in = (uint8_t)(in << 1);
        if (SPI_MISO_GPIO->PDIR & MISO_BIT) in |= 1U;
        SpiHalfDelay(); /* SCK high */
    }
    return in;
}

static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg) {
    uint8_t crc = 0x01;
    if (cmd == 0) crc = 0x95;
    if (cmd == 8) crc = 0x87;

    SD_SPI_Xfer(0xFF);
    SD_SPI_Xfer((uint8_t)(0x40 | cmd));
    SD_SPI_Xfer((uint8_t)(arg >> 24));
    SD_SPI_Xfer((uint8_t)(arg >> 16));
    SD_SPI_Xfer((uint8_t)(arg >> 8));
    SD_SPI_Xfer((uint8_t)arg);
    SD_SPI_Xfer(crc);

    uint8_t r1;
    int n = 12;
    do { r1 = SD_SPI_Xfer(0xFF); } while ((r1 & 0x80) && --n);
    return r1;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0U) return STA_NOINIT;

    /* Own our bus/CS pin clocks explicitly -- don't assume init order vs
     * ui_display_init()/AudioEngine_Init(), CLOCK_EnableClock is idempotent. */
    CLOCK_EnableClock(kCLOCK_GatePORT1); CLOCK_EnableClock(kCLOCK_GateGPIO1);
    CLOCK_EnableClock(kCLOCK_GatePORT2); CLOCK_EnableClock(kCLOCK_GateGPIO2);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn); RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT2_RST_SHIFT_RSTn); RESET_ReleasePeripheralReset(kGPIO2_RST_SHIFT_RSTn);

    PORT_SetPinMux(PORT1, SD_CS_PIN, kPORT_MuxAlt0);
    gpio_pin_config_t cs_cfg = {kGPIO_DigitalOutput, 1};
    GPIO_PinInit(SD_CS_GPIO, SD_CS_PIN, &cs_cfg);
    CS_HIGH();

    /* MISO: input, input buffer must be explicitly enabled on MCXA (same
     * gotcha noted in main.c for the button GPIOs). SCK/MOSI are already
     * driven as outputs by ui_display_init(); MISO is the only new pin.
     * Internal pull-up so a missing/empty card slot reads a clean idle-high
     * line (SD_SendCmd's response wait then times out to 0xFF, i.e. "no
     * card") instead of a floating, noisy input. */
    port_pin_config_t miso_cfg = {0};
    miso_cfg.mux = kPORT_MuxAlt0;
    miso_cfg.pullSelect = kPORT_PullUp;
    miso_cfg.inputBuffer = kPORT_InputBufferEnable;
    PORT_SetPinConfig(PORT2, SPI_MISO_PIN, &miso_cfg);
    gpio_pin_config_t in_cfg = {kGPIO_DigitalInput, 0};
    GPIO_PinInit(SPI_MISO_GPIO, SPI_MISO_PIN, &in_cfg);

    /* Everything from here to the end of identification runs at <=400kHz. */
    s_spi_half_delay = SystemCoreClock / (SD_INIT_CLOCK_HZ * 2U * SD_DELAY_CYCLES_PER_IT);

    CS_HIGH();
    for (int i = 0; i < 10; i++) SD_SPI_Xfer(0xFF); /* >=74 dummy clocks with CS high */

    /* A card that just saw its first clocks often needs CMD0 more than once
     * before it answers 0x01, so give it several tries rather than declaring
     * the slot empty on the first miss. */
    uint8_t r1 = 0xFF;
    for (int attempt = 0; attempt < 8; attempt++) {
        CS_LOW();
        r1 = SD_SendCmd(0, 0); /* GO_IDLE_STATE */
        if (r1 == 0x01) break;
        CS_HIGH();
        SD_SPI_Xfer(0xFF);
    }
    SD_TRACE("[sd] CMD0 -> 0x%02X\r\n", r1);
    if (r1 != 0x01) {
        /* 0xFF here means nothing ever drove MISO: no card, or a wiring
         * problem on MISO/CS. Any other value means the card is talking but
         * didn't go idle. */
        CS_HIGH();
        s_spi_half_delay = 0;
        s_status = STA_NOINIT;
        return s_status;
    }

    uint8_t ocr[4];
    s_card_type = 0;
    r1 = SD_SendCmd(8, 0x1AAU); /* CMD8: check voltage range, distinguishes SDv2 */
    SD_TRACE("[sd] CMD8 -> 0x%02X\r\n", r1);
    if (r1 == 0x01) {
        for (int i = 0; i < 4; i++) ocr[i] = SD_SPI_Xfer(0xFF);
        SD_TRACE("[sd] CMD8 echo %02X %02X %02X %02X\r\n", ocr[0], ocr[1], ocr[2], ocr[3]);
        if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
            int timeout = SD_IDLE_POLL_TRIES;
            do {
                SD_SendCmd(55, 0);
                r1 = SD_SendCmd(41, 0x40000000U); /* ACMD41 with HCS bit (SDv2) */
            } while (r1 != 0 && --timeout);
            SD_TRACE("[sd] ACMD41 -> 0x%02X (%d tries left)\r\n", r1, timeout);
            if (timeout && r1 == 0) {
                SD_SendCmd(58, 0); /* READ_OCR */
                for (int i = 0; i < 4; i++) ocr[i] = SD_SPI_Xfer(0xFF);
                s_card_type = (uint8_t)((ocr[0] & 0x40) ? (2 | 4) : 2);
            }
        }
    } else {
        SD_SendCmd(55, 0);
        r1 = SD_SendCmd(41, 0);
        if (r1 <= 1) {
            int timeout = SD_IDLE_POLL_TRIES;
            do {
                SD_SendCmd(55, 0);
                r1 = SD_SendCmd(41, 0);
            } while (r1 != 0 && --timeout);
            if (timeout) s_card_type = 1; /* SDv1 */
        } else {
            int timeout = SD_IDLE_POLL_TRIES;
            do { r1 = SD_SendCmd(1, 0); } while (r1 != 0 && --timeout); /* MMCv3 */
            if (timeout) s_card_type = 8;
        }
    }
    CS_HIGH();
    SD_SPI_Xfer(0xFF);

    /* Identification is over; block transfers can run at full bit-bang speed. */
    s_spi_half_delay = 0;

    SD_TRACE("[sd] card_type=0x%02X -> %s\r\n", s_card_type,
             s_card_type ? "READY" : "NOT DETECTED");

    s_status = (s_card_type == 0) ? STA_NOINIT : 0;
    return s_status;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0U) return STA_NOINIT;
    return s_status;
}

static bool SD_ReadBlock(uint32_t sector, uint8_t *buf) {
    uint32_t addr = (s_card_type & 4U) ? sector : sector * 512U;
    CS_LOW();
    uint8_t r1 = SD_SendCmd(17, addr); /* READ_SINGLE_BLOCK */
    if (r1 != 0x00) { CS_HIGH(); return false; }

    uint8_t token;
    int timeout = 100000;
    do { token = SD_SPI_Xfer(0xFF); } while (token == 0xFF && --timeout);
    if (token != 0xFE) { CS_HIGH(); return false; }

    for (int i = 0; i < 512; i++) buf[i] = SD_SPI_Xfer(0xFF);
    SD_SPI_Xfer(0xFF); SD_SPI_Xfer(0xFF); /* CRC, ignored */
    CS_HIGH();
    SD_SPI_Xfer(0xFF);
    return true;
}

static bool SD_WriteBlock(uint32_t sector, const uint8_t *buf) {
    uint32_t addr = (s_card_type & 4U) ? sector : sector * 512U;
    CS_LOW();
    uint8_t r1 = SD_SendCmd(24, addr); /* WRITE_BLOCK */
    if (r1 != 0x00) { CS_HIGH(); return false; }

    SD_SPI_Xfer(0xFF);
    SD_SPI_Xfer(0xFE); /* data token */
    for (int i = 0; i < 512; i++) SD_SPI_Xfer(buf[i]);
    SD_SPI_Xfer(0xFF); SD_SPI_Xfer(0xFF); /* dummy CRC */

    uint8_t resp = SD_SPI_Xfer(0xFF);
    if ((resp & 0x1F) != 0x05) { CS_HIGH(); return false; } /* data rejected */

    int timeout = 500000;
    while (SD_SPI_Xfer(0xFF) == 0x00 && --timeout) {} /* wait while card is busy programming */
    CS_HIGH();
    SD_SPI_Xfer(0xFF);
    return timeout != 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0U) return RES_PARERR;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (!SD_ReadBlock((uint32_t)sector + i, buff + (size_t)i * 512U)) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0U) return RES_PARERR;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (!SD_WriteBlock((uint32_t)sector + i, buff + (size_t)i * 512U)) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0U) return RES_PARERR;
    switch (cmd) {
        case CTRL_SYNC:       return RES_OK;
        case GET_SECTOR_SIZE: *(WORD *)buff = 512U; return RES_OK;
        case GET_BLOCK_SIZE:  *(DWORD *)buff = 1U; return RES_OK;
        default:              return RES_PARERR; /* GET_SECTOR_COUNT etc. not needed: FF_USE_MKFS=0, we never format */
    }
}
