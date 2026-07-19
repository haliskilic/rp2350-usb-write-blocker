/*
 * readahead.h - Cekirdek-1 SD on-okuma motoru
 *
 * Amac: MSC okuma yolundaki "SD'den oku -> sonra USB'ye gonder"
 * serilestirmesini kirmak. Cekirdek-1, aktif okuma akisinin devamini
 * SRAM'deki halka tampona onceden ceker; cekirdek-0'in MSC geri
 * cagirmasi isabet durumunda yalnizca memcpy yapar. Sirali okumalar
 * boylece USB hattinin tavanina (~1,1 MB/s) cikar.
 *
 * Es zamanlilik: SD surucusu (carlk3) blok islemlerini kart basina
 * pico mutex'i ile korur (sd_lock) -> iki cekirdek ayni anda surucuye
 * girse de islemler siralanir. Dosya sistemi YALNIZCA cekirdek-0'dan
 * yazilir; her yazma sonrasi readahead_invalidate() cagrilarak bayat
 * veri servis edilmesi engellenir.
 */
#ifndef APP_READAHEAD_H
#define APP_READAHEAD_H

#include <stdbool.h>
#include <stdint.h>

/* Cekirdek-1'i baslat. Depolama init'i (sd_init_driver) sonrasi,
 * yalnizca normal modda (teshis modunda degil) cagrilmali. */
void    readahead_start(void);

/* MSC READ(10) servis noktasi: isabet -> memcpy; iska -> dogrudan SD
 * okumasi + akisi bu noktanin devamina yonlendir. Basarida bufsize,
 * hatada -1 dondurur. */
int32_t readahead_read(uint32_t lba, void *buffer, uint32_t bufsize);

/* Tum on-okunmus veriyi gecersiz kil (her turlu yazma ve mod gecisi
 * sonrasi cagrilir). Akis, bir sonraki okuma iskasyla kendini yeniden
 * kurar. */
void    readahead_invalidate(void);

/* Cekirdek-1 calisiyor mu? (buton lockout karari icin) */
bool    readahead_running(void);

/* -------- Arkadan-yazma (write-behind) --------
 * MSC WRITE(10) verisi SRAM halkasina kopyalanip hemen onaylanir;
 * cekirdek-1 arkada SD'ye bosaltir. Boylece USB alimi ile SD yazimi
 * ortusur. Siralama FIFO'dur (FAT tutarliligi icin sart). */

/* Yazmayi kuyrukla. Halka doluysa bosalana kadar bekler (watchdog
 * beslenir). Basarida bufsize, kalici yazma hatasinda -1. */
int32_t writebehind_enqueue(uint32_t lba, const void *data, uint32_t bufsize);

/* Bekleyen tum yazmalar SD'ye inene kadar blokla. Kalici hata varsa
 * false. Okuma servisi, mod gecisi ve SCSI SYNC oncesi cagrilir. */
bool    writebehind_drain(void);

/* Kuyrukta bekleyen yazma var mi? */
bool    writebehind_pending(void);

/* Kalici yazma-hatasi bayragini temizle. YALNIZCA halka bostayken
 * (drain sonrasi) ve yeni bir yazma oturumu baslarken cagrilmali —
 * kaybolan yazmalar host'a zaten hata olarak raporlanmistir. */
void    writebehind_clear_error(void);

#endif /* APP_READAHEAD_H */
