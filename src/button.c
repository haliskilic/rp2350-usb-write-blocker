/*
 * button.c - BOOTSEL butonu okuma + debounce + kisa/uzun basim ayrimi
 *
 * Kisa basim (birakinca, <1 sn): OKU/YAZ mod degisimi istegi.
 * Uzun basim (>=5 sn): cihazi USB bootloader'a (BOOTSEL) al -> kolay
 * yeniden programlama. Teshis modunda (onceki calisma takildi) HERHANGI
 * bir basim dogrudan BOOTSEL'e alir.
 */
#include "button.h"
#include "mode.h"
#include "diag.h"
#include "readahead.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

#define LONG_PRESS_MS 5000u

/*
 * BOOTSEL butonu, flash cip-secim (CS) hattina baglidir. Durumunu okumak
 * icin CS pinini kisa sureligine yuksek empedansa alip seviyesini oku;
 * bu sirada flash erisimi ve kesmeler devre disi birakilmalidir (kod
 * RAM'den calisir: __no_inline_not_in_flash_func).
 */
static bool __no_inline_not_in_flash_func(bootsel_pressed)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i) { /* seviyenin oturmasi icin bekle */ }

#if PICO_RP2040
    bool pressed = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
#else
    bool pressed = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#endif

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return pressed;
}

static uint32_t s_last_poll_ms;
static uint32_t s_edge_ms;      /* son ham seviye degisiminin zamani */
static uint32_t s_press_ms;     /* gecerli basimin baslangic zamani */
static bool     s_raw_prev;
static bool     s_stable;

void button_init(void) {
    s_last_poll_ms = 0;
    s_edge_ms = 0;
    s_press_ms = 0;
    s_raw_prev = false;
    s_stable = false;
}

void button_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_last_poll_ms < BUTTON_POLL_MS) return;
    s_last_poll_ms = now;

    /* BOOTSEL okumasi flash'i ~us mertebesinde devre disi birakir.
     * Cekirdek-1 o anda flash'tan kod calistiriyorsa cop komut ceker
     * ve coker -> once RAM'e parklat (lockout). Parklatilamazsa bu
     * turda butonu okuma (onceki degeri koru). */
    bool locked = false;
    if (readahead_running()) {
        if (!multicore_lockout_start_timeout_us(500)) return;
        locked = true;
    }
    bool raw = bootsel_pressed();
    if (locked) multicore_lockout_end_timeout_us(500);

    if (raw != s_raw_prev) {
        /* ham seviye degisti -> debounce sayacini baslat */
        s_raw_prev = raw;
        s_edge_ms = now;
        return;
    }

    if ((now - s_edge_ms) >= BUTTON_DEBOUNCE_MS && raw != s_stable) {
        /* seviye BUTTON_DEBOUNCE_MS boyunca sabit kaldi -> gecerli kenar */
        s_stable = raw;
        if (s_stable) {
            /* basma kenari */
            s_press_ms = now;
            if (diag_prev_hang_stage() != 0) {
                /* teshis modunda tek basim -> dogrudan bootloader */
                reset_usb_boot(0, 0);
            }
        } else {
            /* birakma kenari: kisa basimsa mod degisimi */
            if ((now - s_press_ms) < LONG_PRESS_MS) {
                mode_request_toggle();
            }
        }
        return;
    }

    /* uzun basim: birakmayi beklemeden bootloader'a gir */
    if (s_stable && (now - s_press_ms) >= LONG_PRESS_MS) {
        reset_usb_boot(0, 0);
    }
}
