/*
 * diag.c - Watchdog + scratch-register tabanli takilma teshisi
 */
#include "diag.h"

#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

/* scratch[1] bu sihirli degeri tasiyorsa scratch[0] gecerli bir asama
 * kodudur. Scratch registerlar watchdog/yazilim reset'inde korunur,
 * yalnizca power-on/brownout'ta sifirlanir -> elektrik kesintisi yanlis
 * pozitif uretmez. */
#define DIAG_MAGIC 0xD1A60001u

#define WDT_TIMEOUT_MS 8000u   /* SDK ust siniri 8388 ms */

static uint32_t s_prev_stage = 0;

void diag_init(void) {
    if (watchdog_hw->scratch[1] == DIAG_MAGIC) {
        s_prev_stage = watchdog_hw->scratch[0];
    }
    /* isareti tuket: bir sonraki temiz acilis 0 gorsun */
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;

    watchdog_enable(WDT_TIMEOUT_MS, true /* debugger takiliyken duraklat */);
}

uint32_t diag_prev_hang_stage(void) { return s_prev_stage; }

void diag_mark(uint32_t stage) {
    watchdog_hw->scratch[0] = stage;
    watchdog_hw->scratch[1] = DIAG_MAGIC;
}

void diag_clear(void) {
    watchdog_hw->scratch[1] = 0;
}

void diag_feed(void) {
    watchdog_update();
}

static uint32_t s_crash_pc = 0;
static uint32_t s_crash_lr = 0;

void diag_set_crash(uint32_t pc, uint32_t lr) {
    s_crash_pc = pc;
    s_crash_lr = lr;
}

uint32_t diag_crash_pc(void) { return s_crash_pc; }
uint32_t diag_crash_lr(void) { return s_crash_lr; }
