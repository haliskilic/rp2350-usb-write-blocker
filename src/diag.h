/*
 * diag.h - Donanim watchdog + takilma-teshisi altyapisi
 *
 * Amac: ana dongu herhangi bir asamada suresiz bloklanirsa donanim
 * watchdog'u cihazi resetler. Riskli her adimdan once asama numarasi
 * watchdog scratch register'ina yazilir (reset'te silinmez, yalnizca
 * power-on'da silinir). Boylece bir sonraki acilista "onceki calisma
 * hangi asamada takildi" bilgisi elde edilir; bu bilgi LCD'de gosterilir
 * ve USB seri numarasina "-S<n>" eki olarak yansitilir (host tarafindan
 * lsusb ile okunabilir).
 *
 * Asama kodlari:
 *   1  = LCD init
 *   2  = sd_init_driver
 *   3  = dosya sistemi mount (f_mount)
 *   4  = log/durum dosya G/C (acilis logu)
 *   10 = MSC disk_initialize (host tetikledi)
 *   11 = MSC disk_read
 *   12 = MSC disk_write
 */
#ifndef APP_DIAG_H
#define APP_DIAG_H

#include <stdint.h>

enum {
    DIAG_ST_DISPLAY  = 1,
    DIAG_ST_SDINIT   = 2,
    DIAG_ST_MOUNT    = 3,
    DIAG_ST_LOGIO    = 4,
    DIAG_ST_MSC_INIT = 10,
    DIAG_ST_MSC_READ = 11,
    DIAG_ST_MSC_WRITE= 12,
};

/* Onceki calismanin takilma asamasini okur (0 = temiz acilis) ve
 * watchdog'u baslatir (~8 sn). board_init'ten hemen sonra cagrilmali. */
void     diag_init(void);

/* Onceki calisma takildiysa asama kodu, yoksa 0. */
uint32_t diag_prev_hang_stage(void);

/* Riskli adimdan once cagrilir: asamayi scratch'e isle. */
void     diag_mark(uint32_t stage);

/* Riskli adim basariyla bitti: isareti kaldir. */
void     diag_clear(void);

/* Watchdog'u besle (ana dongude her turda). */
void     diag_feed(void);

/* Onceki calismadan kurtarilan cokme kaydi (carlk3 crash.c uzerinden).
 * main tarafindan bir kez set edilir; USB seri numarasi ve LCD teshis
 * ekrani tarafindan okunur. pc==0 -> kayit yok. */
void     diag_set_crash(uint32_t pc, uint32_t lr);
uint32_t diag_crash_pc(void);
uint32_t diag_crash_lr(void);

#endif /* APP_DIAG_H */
