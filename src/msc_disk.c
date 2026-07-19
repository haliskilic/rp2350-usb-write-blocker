/*
 * msc_disk.c - USB MSC (Mass Storage) geri cagirmalari
 *
 * SD kartina ham blok (512 bayt) erisimi FatFs disk_* katmani uzerinden
 * yapilir. Ayni katmani cihaz tarafi loglama (f_*) da kullanir; tek
 * cekirdekli kooperatif dongude sirali calistiklari icin cakismazlar.
 *
 * Mod kilidi:
 *   - MODE_READ: yazma istekleri reddedilir (WRITE PROTECTED). Host salt-okunur.
 *   - MODE_RW  : yazma serbest.
 * Medya, mod gecisi sirasinda kisa sureligine "yok" gosterilir (bkz. mode.c).
 */
#include <assert.h>
#include <string.h>
#include <tusb.h>
#include <pico/stdlib.h>

#include "diskio.h"
#include "diag.h"
#include "mode.h"
#include "readahead.h"

#define MSC_LUN 0

/* Birim hazir mi? (medya takili + disk init edilebiliyor)
 * Teshis modunda (onceki calisma takildi) diske hic dokunulmaz. */
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (diag_prev_hang_stage() != 0) return false;
    if (!mode_media_present()) return false;
    diag_mark(DIAG_ST_MSC_INIT);
    DSTATUS ds = disk_initialize(MSC_LUN);
    diag_clear();
    return (!(STA_NOINIT & ds) && !(STA_NODISK & ds));
}

/* Uretici/urun bilgisi (INQUIRY) */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
    (void)lun;
    snprintf((char *)vendor_id, 8, "%s", "HKILIC");
    snprintf((char *)product_id, 16, "%s", "Write Blocker");
    snprintf((char *)product_rev, 4, "%s", "1.0");
}

/* Kapasite: blok sayisi + blok boyutu (512). FF_LBA64=1 oldugu icin
 * sektor sayisi 64-bit okunur, sonra 32-bit'e sigdirlir. */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count_p, uint16_t *block_size_p) {
    (void)lun;
    *block_size_p = 512;
    if (!tud_msc_test_unit_ready_cb(lun)) {
        *block_count_p = 0;
        return;
    }
    uint64_t sector_count = 0;
    DRESULT dr = disk_ioctl(MSC_LUN, GET_SECTOR_COUNT, &sector_count);
    *block_count_p = (RES_OK == dr) ? (uint32_t)sector_count : 0;
}

/* Yazilabilir mi? -> yalnizca YAZ modunda ve medya takiliyken */
bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    if (!mode_media_present()) return false;
    return (mode_get() == MODE_RW);
}

/* START STOP UNIT: host eject/senkron istegi */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun; (void)power_condition;
    if (load_eject && !start) {
        readahead_invalidate();
        if (!writebehind_drain()) return false;  /* bekleyenler SD'ye insin */
        disk_ioctl(MSC_LUN, CTRL_SYNC, 0);
    }
    return true;
}

/* READ(10): her zaman izinli (medya takiliyken) */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                          uint32_t bufsize) {
    /* Savunmaci kontrol: assert Release'te (NDEBUG) derlenmez; sozlesme
     * ihlali sessizce yanlis veri servis etmek yerine hata dondursun. */
    if (offset != 0 || (bufsize % 512u) != 0) return -1;
    if (!tud_msc_test_unit_ready_cb(lun)) return -1;
    diag_mark(DIAG_ST_MSC_READ);
    int32_t r = readahead_read(lba, buffer, bufsize);  /* isabet: memcpy, iska: SD */
    diag_clear();
    return r;
}

/* WRITE(10): yalnizca YAZ modunda; aksi halde WRITE PROTECTED */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                           uint32_t bufsize) {
    if (offset != 0 || (bufsize % 512u) != 0) return -1;  /* bkz. read10 notu */
    if (!tud_msc_test_unit_ready_cb(lun)) return -1;

    if (mode_get() != MODE_RW) {
        /* Salt-okunur modda yazma reddi: SCSI DATA PROTECT / WRITE PROTECTED */
        tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
        return -1;
    }

    diag_mark(DIAG_ST_MSC_WRITE);
    /* Arkadan-yazma: veri SRAM halkasina kopyalanir, USB hemen devam
     * eder; cekirdek-1 arkada SD'ye bosaltir. */
    int32_t r = writebehind_enqueue(lba, buffer, bufsize);
    diag_clear();
    readahead_invalidate();  /* on-okunmus veri bayatladi */
    return r;
}

/* Diger SCSI komutlari: desteklenmiyor */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {
    (void)buffer; (void)bufsize;
    switch (scsi_cmd[0]) {
        case 0x35:  /* SYNCHRONIZE CACHE (10): bekleyen yazmalari indir */
            if (!writebehind_drain()) {
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x03, 0x00);
                return -1;
            }
            disk_ioctl(MSC_LUN, CTRL_SYNC, 0);
            return 0;
        case 0x1E:  /* PREVENT/ALLOW MEDIUM REMOVAL: kabul et */
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}
