/*
 * mode.h - Oku/Yaz mod durumu ve mod gecis makinesi
 */
#ifndef APP_MODE_H
#define APP_MODE_H

#include "config.h"

/* Baslangic: MODE_READ, medya host'a "takili" */
void        mode_init(void);

/* Aktif mod */
app_mode_t  mode_get(void);

/* MSC katmani icin: medya su an host'a gorunur mu?
 * Mod gecisi sirasinda kisa sureligine 'false' olur (cikar/tak taklidi). */
bool        mode_media_present(void);

/* Buton kesme/anket koduyla cagrilir: bir sonraki mode_task()'te
 * mod degisimi (toggle) istegini kuyruklar. */
void        mode_request_toggle(void);

/* Ana dongude periyodik cagrilir. Bekleyen toggle istegini isler:
 * medyayi cikar -> FS sahipligini devret -> modu degistir -> medyayi tak. */
void        mode_task(void);

#endif /* APP_MODE_H */
