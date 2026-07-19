/*
 * button.h - Kart uzerindeki BOOT butonu (BOOTSEL) ile mod degistirme
 *
 * RP2350-GEEK'te kullanici erisimine acik tek buton BOOT butonudur.
 * Calisma sirasinda basilmasi bootloader'a girmez (bootloader yalnizca
 * reset aninda ornekler); bu yuzden guvenle "kullanici butonu" olarak
 * okunabilir.
 */
#ifndef APP_BUTTON_H
#define APP_BUTTON_H

void button_init(void);

/* Ana dongude periyodik cagrilir. Debounce'lu bir basim tespit edilirse
 * mode_request_toggle() cagrilir. */
void button_task(void);

#endif /* APP_BUTTON_H */
