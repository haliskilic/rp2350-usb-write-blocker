/*
 * display.h - 1.14" ST7789 LCD uzerinde buyuk mod gostergesi
 */
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "config.h"

/* Uygulama katmani icin RGB565 renk sabitleri (LCD kutuphanesinden bagimsiz) */
#define CLR_BLACK  0x0000
#define CLR_WHITE  0xFFFF
#define CLR_RED    0xF800
#define CLR_GREEN  0x07E0
#define CLR_BLUE   0x001F

/* LCD'yi baslat (spi1 + arka isik). */
void display_init(void);

/* Verilen modu tam ekran, buyuk yazi ile goster:
 *   MODE_READ -> yesil zemin, "OKU (R)"
 *   MODE_RW   -> kirmizi zemin, "YAZ (RW)" */
void display_show_mode(app_mode_t mode);

/* Kisa bir baslangic/durum ekrani (kart yoksa uyari vb.) */
void display_show_message(const char *line1, const char *line2, uint16_t bg);

#endif /* APP_DISPLAY_H */
