/*
 * main.c - RP2350-GEEK "OKU/YAZ" USB Mass Storage firmware
 *
 * Akis (saglamlik icin USB once ayaga kaldirilir):
 *   1) board + watchdog/teshis + USB baslat, tud_task dongusune gir
 *   2) USB enumerasyonu icin kisa bir yerlesme suresi
 *   3) Dongu icinde tek sefer: LCD init -> SD/dosya sistemi init + acilis logu
 *   4) Normal calisma: USB + buton + mod gecisi + heartbeat log
 *
 * Takilma korumasi: her riskli adim diag_mark ile isaretlenir; adim
 * suresiz bloklanirsa watchdog (~8 sn) cihazi resetler. Bir sonraki
 * aciliste takilan asama LCD'de ve USB seri numarasinda (-S<n>) raporlanir
 * ve depolama adimlari atlanir (cihaz teshis modunda ayakta kalir).
 *
 * Cekirdek modeli: cekirdek-0 kooperatif dongu (USB + uygulama);
 * cekirdek-1 SD on-okuma/arkadan-yazma motoru (readahead.c). LCD spi1,
 * SD ise SDIO 4-bit (pio1) uzerindedir; dosya sistemine ayni anda
 * yalnizca tek taraf yazar (bkz. mode.c).
 */
#include <stdio.h>

#include <bsp/board.h>
#include <tusb.h>
#include <pico/stdlib.h>

#include "config.h"
#include "diag.h"
#include "mode.h"
#include "logger.h"
#include "button.h"
#include "display.h"
#include "readahead.h"

#include "hw_config.h"   /* sd_init_driver() */
#include "crash.h"       /* carlk3 cokme kaydi (RAM'de reset'i atlatir) */

enum init_stage {
    STAGE_USB_SETTLE = 0,  /* USB enumerasyonu icin bekle */
    STAGE_DISPLAY,         /* LCD init */
    STAGE_STORAGE,         /* SD + dosya sistemi + acilis logu */
    STAGE_RUN              /* normal calisma */
};

int main(void) {
    board_init();
    diag_init();           /* watchdog + onceki takilma bilgisi */
    mode_init();
    button_init();

    /* Onceki calismadan kalan cokme kaydini kurtar (PC/LR fault
     * cercevesi .uninitialized_data'da NVIC reset'i atlatir). */
    crash_handler_init();
    const crash_info_t *ci = crash_handler_get_info();
    if (ci && (ci->magic == crash_magic_hard_fault ||
               ci->magic == crash_magic_stack_overflow ||
               ci->magic == crash_magic_debug_mon)) {
        diag_set_crash(ci->cy_faultFrame.pc, ci->cy_faultFrame.lr);
    }

    const uint32_t prev_hang = diag_prev_hang_stage();

    /* USB'yi hemen baslat (tek MSC arayuzu, salt-okunur baslar) */
    tud_init(BOARD_TUD_RHPORT);

    enum init_stage stage = STAGE_USB_SETTLE;
    absolute_time_t settle_deadline = make_timeout_time_ms(600);

    while (true) {
        diag_feed();
        tud_task();          /* USB olaylarini her zaman isle */

        switch (stage) {
            case STAGE_USB_SETTLE:
                if (time_reached(settle_deadline)) stage = STAGE_DISPLAY;
                break;

            case STAGE_DISPLAY:
                if (prev_hang == DIAG_ST_DISPLAY) {
                    /* onceki calisma LCD init'te takildi: ekrana dokunma */
                    stage = STAGE_STORAGE;
                    break;
                }
                diag_mark(DIAG_ST_DISPLAY);
                display_init();
                diag_clear();
                display_show_message("RP2350-GEEK", "Baslatiliyor...", CLR_BLUE);
                stage = STAGE_STORAGE;
                break;

            case STAGE_STORAGE: {
                if (prev_hang != 0) {
                    /* Teshis modu: depolamaya dokunma, durumu raporla */
                    char l2[32];
                    if (diag_crash_pc() != 0) {
                        snprintf(l2, sizeof(l2), "S%u PC %08lX",
                                 (unsigned)prev_hang, (unsigned long)diag_crash_pc());
                    } else {
                        snprintf(l2, sizeof(l2), "ASAMA S%u", (unsigned)prev_hang);
                    }
                    if (prev_hang != DIAG_ST_DISPLAY) {
                        display_show_message("TAKILDI!", l2, CLR_RED);
                    }
                    stage = STAGE_RUN;
                    break;
                }

                diag_mark(DIAG_ST_SDINIT);
                sd_init_driver();
                diag_clear();

                bool fs_ok = logger_init();   /* icinde MOUNT/LOGIO isaretleri var */
                if (!fs_ok) {
                    display_show_message("SD UYARISI", "Log devre disi", CLR_RED);
                    sleep_ms(1000);
                }
                readahead_start();            /* cekirdek-1 on-okuma motoru */
                display_show_mode(mode_get());
                stage = STAGE_RUN;
                break;
            }

            case STAGE_RUN:
                button_task();               /* toggle / uzun basim / teshis-BOOTSEL */
                if (prev_hang == 0) {
                    mode_task();             /* bekleyen mod gecisini uygula */
                    logger_task(mode_get()); /* MODE_READ'de periyodik heartbeat */
                }
                break;
        }
    }
    return 0;
}
