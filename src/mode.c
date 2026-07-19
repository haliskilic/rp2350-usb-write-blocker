/*
 * mode.c - Oku/Yaz mod gecis makinesi
 *
 * Tasarim (host ile cihaz arasinda dosya sistemi tutarliligi):
 *   - MODE_READ (varsayilan): Host salt-okunur. Yazma istekleri MSC
 *     katmaninda reddedilir. Dosya sistemine YALNIZCA cihaz yazar
 *     (log kayitlari). Boylece iki taraf ayni anda FAT'e yazmaz.
 *   - MODE_RW: Host yazma sahibidir. Cihaz FatFs'i birakir (unmount),
 *     bu sirada log kayitlari RAM tamponuna alinir.
 *
 * Mod her degistiginde medya kisa sureligine "cikarilmis" gosterilir
 * (test_unit_ready = false). Host bunu medya degisimi olarak algilar ve
 * geri "takildiginda" FAT tablosunu + yazma-korumasi durumunu yeniden
 * okur. Bu, salt-okunurdan yaz moduna geciste host'un guncel FAT ile
 * baslamasini ve bozulma olmamasini saglar.
 */
#include "mode.h"
#include "diag.h"
#include "logger.h"
#include "display.h"
#include "readahead.h"

#include "tusb.h"
#include "pico/stdlib.h"

static volatile app_mode_t s_mode;
static volatile bool       s_media_present;
static volatile bool       s_toggle_pending;

void mode_init(void) {
    s_mode = MODE_READ;
    s_media_present = true;
    s_toggle_pending = false;
}

app_mode_t mode_get(void)          { return s_mode; }
bool       mode_media_present(void){ return s_media_present; }

void mode_request_toggle(void) {
    s_toggle_pending = true;
}

/* Belirtilen sure boyunca USB yiginini beslemeye devam et.
 * (Host'un medya durum degisimini gorebilmesi icin sart.) */
static void pump_usb_ms(uint32_t ms) {
    absolute_time_t deadline = make_timeout_time_ms(ms);
    do {
        diag_feed();
        tud_task();
    } while (!time_reached(deadline));
}

void mode_task(void) {
    if (!s_toggle_pending) {
        return;
    }
    s_toggle_pending = false;

    app_mode_t new_mode = (s_mode == MODE_READ) ? MODE_RW : MODE_READ;

    /* 1) Medyayi host gozunde "cikar": bekleyen yazmalar bosaltilsin,
     *    host geri takildiginda tablolari yeniden okusun. */
    readahead_invalidate();
    s_media_present = false;
    pump_usb_ms(EJECT_SETTLE_MS);

    /* Host'un kuyruktaki yazmalari SD'ye inmeden dosya sistemine
     * dokunulmamali (OKU'ya donuste FatFs mount taze veriyi gormeli). */
    writebehind_drain();

    /* 2) Dosya sistemi sahipligini devret + olayi logla */
    if (new_mode == MODE_RW) {
        /* Host'a yazma erisimi verilecek: once bu olayi yaz, sonra FS'i birak.
         * Onceki oturumdan kalan yapiskan yazma-hatasi bayragi yeni oturum
         * icin temizlenir (kayiplar host'a o sirada raporlanmisti; bayrak
         * kalirsa cihaz guc cevrimine kadar kalici "yazamaz" olurdu). */
        writebehind_clear_error();
        logger_log_button(MODE_READ, MODE_RW);
        logger_unmount();
    } else {
        /* Salt-okunura donuluyor: host'un yaptigi degisiklikleri gormek icin
         * FS yeniden baglanir, ardindan olay + bekleyen loglar yazilir. */
        logger_mount();
        logger_log_button(MODE_RW, MODE_READ);
    }

    /* 3) Modu uygula ve medyayi geri tak */
    s_mode = new_mode;
    s_media_present = true;

    /* Host'a "medya degisti, kapasite/koruma yeniden okunmali" bilgisini
     * bir sonraki komutta iletmek icin UNIT ATTENTION ayarla. */
    tud_msc_set_sense(0, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);

    /* 4) Ekrani guncelle */
    display_show_mode(new_mode);

    pump_usb_ms(50);
}
