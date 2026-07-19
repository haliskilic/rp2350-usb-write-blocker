/*
 * display.c - ST7789 (240x135) buyuk mod gostergesi
 *
 * Waveshare LCD kutuphanesi (DEV_Config + LCD_1IN14_V2 + GUI_Paint) uzerine
 * kuruludur. Font24 (17x24) glifleri 2x olceklenerek gercekten buyuk
 * ("kocaman") metin cizilir.
 */
#include "display.h"

#include "DEV_Config.h"
#include "LCD_1in14_V2.h"
#include "GUI_Paint.h"
#include "fonts.h"

#include <string.h>

/* Yatay yerlesimde cozunurluk: 240 (genislik) x 135 (yukseklik) */
#define SCR_W 240
#define SCR_H 135

static UWORD s_image[SCR_W * SCR_H];

/* --- Font24 glifini 'scale' katiyla buyuk ciz --- */
static void draw_big_char(int x, int y, char ch, int scale, UWORD fg) {
    if (ch < ' ' || ch > '~') return;
    const int fw = Font24.Width;    /* 17 */
    const int fh = Font24.Height;   /* 24 */
    const int bytes_per_row = (fw + 7) / 8;               /* 3 */
    const uint8_t *glyph = &Font24.table[(ch - ' ') * bytes_per_row * fh];

    for (int row = 0; row < fh; row++) {
        for (int col = 0; col < fw; col++) {
            uint8_t byte = glyph[row * bytes_per_row + (col / 8)];
            if (byte & (0x80 >> (col % 8))) {
                int px = x + col * scale;
                int py = y + row * scale;
                Paint_DrawRectangle(px, py, px + scale - 1, py + scale - 1,
                                    fg, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            }
        }
    }
}

/* --- Ortalanmis buyuk metin (otomatik sigdirmali) ---
 * Dogal ilerleme (glif + bosluk) ekrana sigmazsa karakter araligi
 * daraltilir: or. "YAZ (RW)" 8 karakter x 36 px = 286 px > 240 px
 * tasiyordu (W yarim, parantez hic cizilmiyordu); 240/8 = 30 px
 * ilerlemeyle tam sigar (simulasyonla dogrulandi). */
static void draw_big_text(int y, const char *s, int scale, UWORD fg) {
    int fw  = Font24.Width;
    int len = (int)strlen(s);
    if (len <= 0) return;
    int adv = fw * scale + scale;           /* dogal: glif + bosluk */
    if (len * adv > SCR_W) adv = SCR_W / len;  /* sigdir */
    int total = len * adv;
    int x = (SCR_W - total) / 2;
    if (x < 0) x = 0;
    for (const char *p = s; *p; ++p) {
        draw_big_char(x, y, *p, scale, fg);
        x += adv;
    }
}

/* --- Ortalanmis normal (Font16) metin --- */
static void draw_center16(int y, const char *s, UWORD fg, UWORD bg) {
    int w = (int)strlen(s) * Font16.Width;
    int x = (SCR_W - w) / 2;
    if (x < 0) x = 0;
    Paint_DrawString_EN(x, y, s, &Font16, fg, bg);
}

void display_init(void) {
    DEV_Module_Init();
    LCD_1IN14_V2_Init(HORIZONTAL);
    LCD_1IN14_V2_Clear(BLACK);
    DEV_SET_PWM(90);   /* arka isik ~%90 */
    Paint_NewImage((UBYTE *)s_image, SCR_W, SCR_H, ROTATE_0, WHITE);
    Paint_SetScale(65);
    Paint_Clear(BLACK);
    LCD_1IN14_V2_Display(s_image);
}

void display_show_mode(app_mode_t mode) {
    UWORD bg = (mode == MODE_RW) ? RED : GREEN;
    UWORD fg = WHITE;
    const char *big = (mode == MODE_RW) ? "YAZ (RW)" : "OKU (R)";
    const char *sub = (mode == MODE_RW) ? "HOST YAZABILIR" : "SALT-OKUNUR";

    Paint_SelectImage((UBYTE *)s_image);
    Paint_Clear(bg);
    /* ust kenar serdi */
    Paint_DrawRectangle(0, 0, SCR_W - 1, SCR_H - 1, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    draw_big_text(28, big, 2, fg);     /* 2x -> ~34x48 px, cok belirgin */
    draw_center16(96, sub, fg, bg);

    LCD_1IN14_V2_Display(s_image);
}

void display_show_message(const char *line1, const char *line2, uint16_t bg) {
    Paint_SelectImage((UBYTE *)s_image);
    Paint_Clear(bg);
    if (line1) draw_center16(45, line1, WHITE, bg);
    if (line2) draw_center16(75, line2, WHITE, bg);
    LCD_1IN14_V2_Display(s_image);
}
