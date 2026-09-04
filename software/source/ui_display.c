#include "ui_display.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_reset.h"
#include "fsl_common.h"
#include "audio_engine.h"
#include "sdcard_wav.h"
#include "wifi_esp.h"

/* ==========================================================================
 * LCD hardware driver (bit-banged SPI to a real ILI9341, 240x320 portrait).
 * This part is dictated by the physical display controller's command set,
 * not a design choice -- left as-is from the proven-working version.
 * ========================================================================== */

#define LCD_CS_PORT GPIO2
#define LCD_CS_PIN  6U
#define LCD_DC_PORT GPIO3
#define LCD_DC_PIN  0U
#define LCD_RST_PORT GPIO2
#define LCD_RST_PIN  5U
#define LCD_SCK_PORT GPIO2
#define LCD_SCK_PIN  12U
#define LCD_MOSI_PORT GPIO2
#define LCD_MOSI_PIN 13U

#define SCK_BIT   (1U << LCD_SCK_PIN)
#define MOSI_BIT  (1U << LCD_MOSI_PIN)
#define CS_BIT    (1U << LCD_CS_PIN)
#define DC_BIT    (1U << LCD_DC_PIN)

static void delay_ms(uint32_t ms) { SDK_DelayAtLeastUs(ms * 1000U, SystemCoreClock); }

static inline void SPI_Write8(uint8_t d) {
    for (uint8_t bit = 0; bit < 8; bit++) {
        GPIO2->PCOR = SCK_BIT;
        if (d & 0x80) GPIO2->PSOR = MOSI_BIT;
        else          GPIO2->PCOR = MOSI_BIT;
        d <<= 1;
        GPIO2->PSOR = SCK_BIT;
    }
}

void lcd_send_cmd(uint8_t cmd) {
    GPIO2->PCOR = CS_BIT; GPIO3->PCOR = DC_BIT;
    SPI_Write8(cmd);
    GPIO2->PSOR = CS_BIT;
}
void lcd_send_data(uint8_t data) {
    GPIO2->PCOR = CS_BIT; GPIO3->PSOR = DC_BIT;
    SPI_Write8(data);
    GPIO2->PSOR = CS_BIT;
}
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_send_cmd(0x2A); lcd_send_data(x0 >> 8); lcd_send_data(x0 & 0xFF); lcd_send_data(x1 >> 8); lcd_send_data(x1 & 0xFF);
    lcd_send_cmd(0x2B); lcd_send_data(y0 >> 8); lcd_send_data(y0 & 0xFF); lcd_send_data(y1 >> 8); lcd_send_data(y1 & 0xFF);
    lcd_send_cmd(0x2C);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    uint8_t ch = color >> 8; uint8_t cl = color & 0xFF;
    GPIO2->PCOR = CS_BIT; GPIO3->PSOR = DC_BIT;
    for (int i = 0; i < w * h; i++) { SPI_Write8(ch); SPI_Write8(cl); }
    GPIO2->PSOR = CS_BIT;
}

void lcd_fill_screen(uint16_t color) { draw_rect(0, 0, 240, 320, color); }

void ui_display_init(void) {
    PORT_SetPinMux(PORT2, 12U, kPORT_MuxAlt0); PORT_SetPinMux(PORT2, 13U, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT2, 6U, kPORT_MuxAlt0); PORT_SetPinMux(PORT3, 0U, kPORT_MuxAlt0); PORT_SetPinMux(PORT2, 5U, kPORT_MuxAlt0);

    gpio_pin_config_t out_config = {kGPIO_DigitalOutput, 1};
    GPIO_PinInit(LCD_SCK_PORT, LCD_SCK_PIN, &out_config); GPIO_PinInit(LCD_MOSI_PORT, LCD_MOSI_PIN, &out_config);
    GPIO_PinInit(LCD_CS_PORT, LCD_CS_PIN, &out_config); GPIO_PinInit(LCD_DC_PORT, LCD_DC_PIN, &out_config); GPIO_PinInit(LCD_RST_PORT, LCD_RST_PIN, &out_config);

    GPIO_PinWrite(LCD_RST_PORT, LCD_RST_PIN, 0); delay_ms(50);
    GPIO_PinWrite(LCD_RST_PORT, LCD_RST_PIN, 1); delay_ms(150);

    lcd_send_cmd(0x01); delay_ms(150);
    lcd_send_cmd(0xCB); lcd_send_data(0x39); lcd_send_data(0x2C); lcd_send_data(0x00); lcd_send_data(0x34); lcd_send_data(0x02);
    lcd_send_cmd(0xCF); lcd_send_data(0x00); lcd_send_data(0XC1); lcd_send_data(0X30);
    lcd_send_cmd(0xE8); lcd_send_data(0x85); lcd_send_data(0x00); lcd_send_data(0x78);
    lcd_send_cmd(0xEA); lcd_send_data(0x00); lcd_send_data(0x00);
    lcd_send_cmd(0xED); lcd_send_data(0x64); lcd_send_data(0x03); lcd_send_data(0X12); lcd_send_data(0X81);
    lcd_send_cmd(0xF7); lcd_send_data(0x20);
    lcd_send_cmd(0xC0); lcd_send_data(0x23);
    lcd_send_cmd(0xC1); lcd_send_data(0x10);
    lcd_send_cmd(0xC5); lcd_send_data(0x3e); lcd_send_data(0x28);
    lcd_send_cmd(0xC7); lcd_send_data(0x86);

    // PORTRAIT FLIPPED 180 DEGREES
    lcd_send_cmd(0x36); lcd_send_data(0x88);

    lcd_send_cmd(0x3A); lcd_send_data(0x55);
    lcd_send_cmd(0xB1); lcd_send_data(0x00); lcd_send_data(0x18);
    lcd_send_cmd(0xB6); lcd_send_data(0x08); lcd_send_data(0x82); lcd_send_data(0x27);
    lcd_send_cmd(0x11); delay_ms(150);
    lcd_send_cmd(0x29); delay_ms(50);
}

/* ==========================================================================
 * Visual design: dark "synth dashboard" theme, full ASCII font, gradient
 * spectrum. Everything below this line is presentation and is free to be
 * whatever looks good -- none of it is dictated by the hardware.
 * ========================================================================== */

/* Deep indigo/navy dashboard palette (RGB565) */
#define COLOR_BG      0x0843 // near-black indigo
#define COLOR_PANEL   0x18C6 // dark slate panel fill
#define COLOR_HEADER  0x2128 // slightly lighter header fill
#define COLOR_BORDER  0x5AD1 // soft slate border
#define COLOR_TEXT    0xFFFF // white
#define COLOR_MUTED   0x94B6 // muted gray-blue text
#define COLOR_ACCENT  0x071F // electric cyan
#define COLOR_ACCENT2 0xB2DF // violet
#define COLOR_WARN    0xFA2B // coral red
#define COLOR_OK      0x3F31 // spring green

/* Spectrum gradient stops: green -> cyan -> violet -> pink, bottom to top */
#define SPEC_C0 0x3732
#define SPEC_C1 0x071F
#define SPEC_C2 0xB2DF
#define SPEC_C3 0xF9F6

static uint8_t active_mode = 0;
static bool redraw_needed = true;
static bool freeze = false;

/* Public-domain 8x8 font (classic CP437-style bitmap font, MSB-first per
 * row), full printable ASCII 32-126 so lowercase text finally renders
 * correctly instead of being forced to uppercase. */
static const uint8_t font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 !
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 35 #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 36 $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 37 %
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 38 &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 40 (
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 41 )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 *
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 44 ,
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 46 .
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 47 /
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 48 0
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 49 1
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 50 2
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 51 3
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 52 4
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 53 5
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 54 6
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 55 7
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 56 8
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 57 9
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 58 :
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 59 ;
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 60 <
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 61 =
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 62 >
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 63 ?
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 64 @
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 65 A
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 66 B
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 67 C
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 68 D
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 69 E
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 70 F
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 71 G
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 72 H
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 73 I
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 74 J
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 75 K
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 76 L
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 77 M
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 78 N
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 79 O
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 80 P
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 81 Q
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 82 R
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 83 S
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 84 T
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 85 U
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 86 V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 W
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 88 X
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 89 Y
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 90 Z
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 91 [
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 92 backslash
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 93 ]
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 _
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 96 `
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 97 a
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 98 b
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 99 c
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 100 d
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 101 e
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 102 f
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 103 g
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 104 h
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 105 i
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 106 j
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 107 k
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 108 l
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 109 m
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 110 n
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 111 o
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 112 p
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 113 q
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 114 r
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 115 s
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 116 t
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 117 u
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 118 v
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 119 w
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 120 x
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 121 y
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 122 z
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 123 {
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 |
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 125 }
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 ~
};

void draw_panel(int x, int y, int w, int h, uint16_t fill_color, uint16_t border_color) {
    draw_rect(x, y, w, h, border_color);
    draw_rect(x+2, y+2, w-4, h-4, fill_color);
}

/* A "card": like draw_panel, but with a colored accent strip along the top
 * edge instead of a uniform border -- the recurring visual motif of this
 * theme (header, status cards). */
static void draw_card(int x, int y, int w, int h, uint16_t fill, uint16_t accent) {
    draw_rect(x, y, w, h, COLOR_BORDER);
    draw_rect(x, y, w, 3, accent);
    draw_rect(x + 2, y + 3, w - 4, h - 5, fill);
}

void draw_char_scaled(int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) c = 32;
    uint8_t c_idx = (uint8_t)(c - 32);
    for (int row = 0; row < 8; row++) {
        uint8_t line = font8x8[c_idx][row];
        for (int col = 0; col < 8; col++) {
            /* This font table is LSB-first per row (bit0 = leftmost pixel) --
             * different from the previous table, which is why text rendered
             * mirrored until this was flipped to match. */
            if (line & (1 << col)) {
                draw_rect(x + col*scale, y + row*scale, scale, scale, color);
            }
        }
    }
}
void draw_string_scaled(int x, int y, const char* str, uint16_t color, int scale) {
    int cx = x;
    while (*str) {
        draw_char_scaled(cx, y, *str, color, scale);
        cx += 8 * scale + 2;
        if (cx >= 240 - (8*scale)) { cx = x; y += 8 * scale + 4; }
        str++;
    }
}

void draw_string(int x, int y, const char* str, uint16_t color) {
    draw_string_scaled(x, y, str, color, 1);
}

static uint16_t lerp565(uint16_t c0, uint16_t c1, uint8_t t) {
    int r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    int r = r0 + ((r1 - r0) * t) / 255;
    int g = g0 + ((g1 - g0) * t) / 255;
    int b = b0 + ((b1 - b0) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Continuous 4-stop gradient (green -> cyan -> violet -> pink) across a
 * 0-200 bar height, instead of the old flat 3-tier VU-meter coloring. */
uint16_t get_gradient_color(int height) {
    if (height < 0) height = 0;
    if (height > 200) height = 200;
    if (height <= 66)       return lerp565(SPEC_C0, SPEC_C1, (uint8_t)(height * 255 / 66));
    else if (height <= 133) return lerp565(SPEC_C1, SPEC_C2, (uint8_t)((height - 66) * 255 / 67));
    else                    return lerp565(SPEC_C2, SPEC_C3, (uint8_t)((height - 133) * 255 / 67));
}

static void draw_footer(const char *label1, uint16_t tint1, const char *label2, uint16_t tint2, const char *label3, uint16_t tint3) {
    draw_panel(5, 280, 70, 35, COLOR_PANEL, tint1);
    draw_string_scaled(15, 292, label1, COLOR_TEXT, 1);
    draw_panel(80, 280, 80, 35, COLOR_PANEL, tint2);
    draw_string_scaled(90, 292, label2, COLOR_TEXT, 1);
    draw_panel(165, 280, 70, 35, COLOR_PANEL, tint3);
    draw_string_scaled(175, 292, label3, COLOR_TEXT, 1);
}

#define APP_MODE_SYNTH 0
#define APP_MODE_SD    1
#define APP_MODE_WIFI  2

void ui_display_mode(uint8_t mode) {
    active_mode = mode;
    redraw_needed = true;
    if (mode != APP_MODE_SD) sdcard_stop_playback();
    if (mode == APP_MODE_SD) sdcard_recheck(); /* no CD pin -- (re)mounting IS the detection */
    if (mode != APP_MODE_SD || !sdcard_is_playing()) AudioEngine_SetMode(AUDIO_MODE_SYNTH);
}

/* ---------------- SYNTHESIZER ---------------- */
#define SPECTRUM_BARS 16 /* divides FFT_SIZE/2 (128) evenly -- every bin gets shown, none dropped */
#define SPECTRUM_BAR_PITCH 13
#define SPECTRUM_BAR_WIDTH 10
static uint8_t s_peak[SPECTRUM_BARS] = {0};
static uint32_t s_blink_counter = 0;

static void draw_synth_screen(void) {
    if (redraw_needed) {
        lcd_fill_screen(COLOR_BG);
        draw_card(5, 5, 230, 44, COLOR_HEADER, COLOR_ACCENT);
        draw_string_scaled(15, 14, "SYNTHESIZER", COLOR_TEXT, 2);
        char buf[24];
        snprintf(buf, sizeof(buf), "filter: %s", AudioEngine_GetVoiceFilterName(AudioEngine_GetVoiceFilter()));
        draw_string_scaled(15, 36, buf, COLOR_ACCENT2, 1);

        draw_footer("MODE", COLOR_BORDER, "FILTER", COLOR_ACCENT2, freeze ? "RESUME" : "FREEZE", freeze ? COLOR_WARN : COLOR_OK);
        for (int i = 0; i < SPECTRUM_BARS; i++) s_peak[i] = 0;
        redraw_needed = false;
    }

    /* Small pulsing dot next to the header confirms the mic->headphone
     * monitoring path is live (see AudioEngine_Process, AUDIO_MODE_SYNTH).
     * BUG FIXED HERE: this used to call draw_rect() -- a full bit-banged SPI
     * transaction -- on every single call to draw_synth_screen(), i.e. every
     * main-loop iteration, thousands of times a second, completely
     * independent of the ~16ms FFT/bar-redraw cadence. That's almost
     * certainly a bigger, more *constant* source of SPI switching noise than
     * the bars ever were. Now it only actually draws when the color changes
     * (twice per blink cycle), like everything else on this screen. */
    s_blink_counter++;
    uint16_t dot_color = (s_blink_counter & 0x10000U) ? COLOR_OK : COLOR_PANEL;
    static uint16_t s_last_dot_color = 0xFFFF;
    if (dot_color != s_last_dot_color) {
        draw_rect(210, 16, 10, 10, dot_color);
        s_last_dot_color = dot_color;
    }

    if (AudioEngine_FFTReady) {
        if (!freeze) {
            const int bins_per_bar = (FFT_SIZE / 2) / SPECTRUM_BARS;
            for (int i = 0; i < SPECTRUM_BARS; i++) {
                uint32_t sum = 0;
                for (int b = 0; b < bins_per_bar; b++) sum += AudioEngine_FFTBins[i * bins_per_bar + b];
                uint8_t h = (uint8_t)((sum / bins_per_bar) * 200U / 255U);

                if (h > s_peak[i]) s_peak[i] = h;

                int x = 10 + i * SPECTRUM_BAR_PITCH;
                draw_rect(x, 270 - 200, SPECTRUM_BAR_WIDTH, 200 - h, COLOR_BG);
                draw_rect(x, 270 - h, SPECTRUM_BAR_WIDTH, h, get_gradient_color(h));
                draw_rect(x, 270 - s_peak[i] - 2, SPECTRUM_BAR_WIDTH, 2, COLOR_TEXT);

                if (s_peak[i] > 0) s_peak[i]--; /* slow decay */
            }
        }
        AudioEngine_FFTReady = false;
    }
}

static void synth_action1(void) { /* SW2: cycle voice filter */
    VoiceFilter_t f = (VoiceFilter_t)((AudioEngine_GetVoiceFilter() + 1) % VOICE_FILTER_COUNT);
    AudioEngine_SetVoiceFilter(f);
    redraw_needed = true;
}
static void synth_action2(void) { /* SW3: freeze/unfreeze the spectrum */
    freeze = !freeze;
    redraw_needed = true;
}

/* ---------------- SD RECORDER ---------------- */
static void draw_sd_screen(void) {
    static uint32_t last_elapsed = 0xFFFFFFFF;
    static bool last_rec = false, last_play = false, last_present = true;
    bool present = sdcard_is_inserted();

    if (redraw_needed) {
        lcd_fill_screen(COLOR_BG);
        draw_card(5, 5, 230, 44, COLOR_HEADER, COLOR_ACCENT);
        draw_string_scaled(15, 20, "SD RECORDER", COLOR_TEXT, 2);

        draw_card(5, 55, 230, 65, COLOR_PANEL, present ? COLOR_OK : COLOR_WARN);
        draw_string_scaled(15, 65, "sd card status", COLOR_MUTED, 1);
        if (present) {
            draw_string_scaled(15, 85, "DETECTED", COLOR_OK, 1);
            char buf[24];
            snprintf(buf, sizeof(buf), "free: %lu kb", (unsigned long)sdcard_free_space_kb());
            draw_string_scaled(15, 103, buf, COLOR_TEXT, 1);
        } else {
            draw_string_scaled(15, 85, "NOT DETECTED", COLOR_WARN, 1);
            draw_string_scaled(15, 103, "recording disabled", COLOR_MUTED, 1);
        }

        draw_footer("MODE", COLOR_BORDER, "REC", COLOR_WARN, "PLAY", COLOR_OK);

        last_elapsed = 0xFFFFFFFF; last_rec = !sdcard_is_recording(); last_play = !sdcard_is_playing(); last_present = !present;
        redraw_needed = false;
    }

    uint32_t elapsed = sdcard_record_elapsed_ms() / 100;
    bool rec = sdcard_is_recording(), play = sdcard_is_playing();
    if (present != last_present || elapsed != last_elapsed || rec != last_rec || play != last_play) {
        last_present = present; last_elapsed = elapsed; last_rec = rec; last_play = play;

        uint16_t stateColor = rec ? COLOR_WARN : play ? COLOR_OK : COLOR_ACCENT2;
        draw_card(5, 135, 230, 100, COLOR_PANEL, present ? stateColor : COLOR_BORDER);
        if (!present) {
            draw_string_scaled(35, 175, "insert sd card", COLOR_WARN, 1);
        } else if (rec) {
            draw_string_scaled(50, 150, "RECORDING", COLOR_WARN, 2);
            char buf[24];
            snprintf(buf, sizeof(buf), "time: %lu.%lus", (unsigned long)(elapsed / 10), (unsigned long)(elapsed % 10));
            draw_string_scaled(45, 190, buf, COLOR_TEXT, 1);
        } else if (play) {
            draw_string_scaled(55, 150, "PLAYING", COLOR_OK, 2);
            char buf[28];
            snprintf(buf, sizeof(buf), "%s", sdcard_current_filename());
            draw_string_scaled(20, 190, buf, COLOR_MUTED, 1);
        } else {
            draw_string_scaled(70, 150, "READY", COLOR_ACCENT, 2);
            char buf[28];
            snprintf(buf, sizeof(buf), "file: %s", sdcard_current_filename());
            draw_string_scaled(15, 190, buf, COLOR_TEXT, 1);
            snprintf(buf, sizeof(buf), "(%u / %u)", sdcard_file_selected_number(), sdcard_file_count());
            draw_string_scaled(15, 208, buf, COLOR_MUTED, 1);
        }
    }
}

static void sd_action1(void) { /* SW2: REC start/stop, blocked entirely without a real card */
    if (!sdcard_is_inserted()) return;
    if (sdcard_is_recording()) sdcard_stop_recording();
    else sdcard_start_recording();
    redraw_needed = true;
}
static void sd_action2(void) { /* SW3: play current file, or stop+advance to the next one */
    if (!sdcard_is_inserted() || sdcard_is_recording()) return;
    if (sdcard_is_playing()) { sdcard_stop_playback(); sdcard_next_file(); }
    else sdcard_start_playback();
    redraw_needed = true;
}

/* ---------------- WIFI SERVER ---------------- */
static bool wifi_show_level = false;

static void draw_wifi_screen(void) {
    static uint32_t last_reqs = 0xFFFFFFFF;
    static wifi_state_t last_state = (wifi_state_t)0xFF;

    if (redraw_needed) {
        lcd_fill_screen(COLOR_BG);
        draw_card(5, 5, 230, 44, COLOR_HEADER, COLOR_ACCENT2);
        draw_string_scaled(15, 20, "WIFI SERVER", COLOR_TEXT, 2);

        draw_card(5, 55, 230, 100, COLOR_PANEL, COLOR_ACCENT2);
        char buf[28];
        snprintf(buf, sizeof(buf), "ssid: %s", wifi_esp_get_ssid());
        draw_string_scaled(15, 65, buf, COLOR_TEXT, 1);
        snprintf(buf, sizeof(buf), "ip: %s", wifi_esp_get_ip());
        draw_string_scaled(15, 83, buf, COLOR_TEXT, 1);

        draw_footer("MODE", COLOR_BORDER, "RESTART", COLOR_ACCENT2, wifi_show_level ? "INFO" : "LEVEL", COLOR_OK);
        last_reqs = 0xFFFFFFFF; last_state = (wifi_state_t)0xFF;
        redraw_needed = false;
    }

    wifi_state_t state = wifi_esp_get_state();
    uint32_t reqs = wifi_esp_get_request_count();
    if (state != last_state || reqs != last_reqs) {
        last_state = state; last_reqs = reqs;
        uint16_t stateColor = (state == WIFI_STATE_READY) ? COLOR_OK : (state == WIFI_STATE_ERROR) ? COLOR_WARN : COLOR_ACCENT;
        draw_rect(15, 103, 210, 12, COLOR_PANEL);
        char buf[28];
        snprintf(buf, sizeof(buf), "state: %s", wifi_esp_get_state_name());
        draw_string_scaled(15, 103, buf, stateColor, 1);
        draw_rect(15, 121, 210, 12, COLOR_PANEL);
        snprintf(buf, sizeof(buf), "requests: %lu", (unsigned long)reqs);
        draw_string_scaled(15, 121, buf, COLOR_MUTED, 1);
    }

    if (wifi_show_level) {
        draw_card(5, 165, 230, 60, COLOR_PANEL, COLOR_OK);
        draw_string_scaled(15, 175, "live input level", COLOR_MUTED, 1);
        uint16_t level = AudioEngine_GetLevel();
        uint32_t w = (level * 200U) / 32767U;
        if (w > 200) w = 200;
        draw_rect(20, 200, w, 12, COLOR_OK);
        draw_rect(20 + w, 200, 200 - w, 12, COLOR_BG);
    }
}

static void wifi_action1(void) { wifi_esp_restart(); redraw_needed = true; }
static void wifi_action2(void) { wifi_show_level = !wifi_show_level; redraw_needed = true; }

/* ---------------- Dispatch ---------------- */

void ui_display_process_loop(void) {
    if (active_mode == APP_MODE_SYNTH) draw_synth_screen();
    else if (active_mode == APP_MODE_SD) draw_sd_screen();
    else draw_wifi_screen();
}

void ui_handle_action1(uint8_t mode) {
    if (mode == APP_MODE_SYNTH) synth_action1();
    else if (mode == APP_MODE_SD) sd_action1();
    else wifi_action1();
}
void ui_handle_action2(uint8_t mode) {
    if (mode == APP_MODE_SYNTH) synth_action2();
    else if (mode == APP_MODE_SD) sd_action2();
    else wifi_action2();
}
