/*
 * logger.h - FAT32 uzerine olay/oturum loglama ve durum kalicilastirma
 *
 * /logs/events.log : insan-okur olay satirlari
 * /logs/state.txt  : oturum durumu (boot sayaci, son uptime, calisma bayragi)
 *                    -> guc kesintisi sonrasi "onceki oturum ne kadar calisti"
 *                       bilgisini uretmek icin periyodik guncellenir.
 */
#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include "config.h"

/* Dosya sistemini bagla, /logs olustur, onceki oturumu degerlendir ve
 * "BOOT" satirini logla. Basari durumunda true. */
bool logger_init(void);

/* Ana dongude periyodik: MODE_READ ve FS bagliyken heartbeat (state.txt)
 * gunceller. Mevcut modu parametre olarak alir. */
void logger_task(app_mode_t mode);

/* FatFs baglama/birakma (mod gecisinde sahiplik devri icin). */
void logger_mount(void);
void logger_unmount(void);

/* Buton kaynakli mod degisimini logla (from -> to). */
void logger_log_button(app_mode_t from, app_mode_t to);

/* Serbest metin olay satiri (dahili kullanim / genisletme icin). */
void logger_log(const char *msg);

#endif /* APP_LOGGER_H */
