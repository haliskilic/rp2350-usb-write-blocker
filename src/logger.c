/*
 * logger.c - FAT32/exFAT uzerine olay ve oturum loglama
 *
 * Not: Log dosyalarinin icerigi, eski/dar kapsamli dosya goruntuleyicilerle
 * de sorunsuz acilabilmesi icin bilincli olarak ASCII (diakritiksiz) tutulur.
 */
#include "logger.h"
#include "diag.h"
#include "readahead.h"

#include "ff.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FATFS    s_fs;
static bool     s_mounted = false;
static uint32_t s_boot_count = 0;
static uint32_t s_last_heartbeat_ms = 0;

/* FS bagli degilken uretilen loglar icin kucuk RAM tamponu */
#define PEND_MAX  8
#define LINE_MAX  160
static char s_pending[PEND_MAX][LINE_MAX];
static int  s_pend_count = 0;

static uint32_t uptime_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static const char *mode_str(app_mode_t m) {
    return (m == MODE_RW) ? "YAZ(RW)" : "OKU(R)";
}

/* --- state.txt yaz (running=1: oturum aktif) --- */
static void write_state(int running) {
    if (!s_mounted) return;
    diag_mark(DIAG_ST_LOGIO);
    FIL f;
    if (f_open(&f, LOG_STATE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) { diag_clear(); return; }
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "# RP2350 USB Write Blocker - " PROJECT_URL "\n"
                     "# iletisim / contact: " PROJECT_CONTACT "\n"
                     "boot_count=%lu\nlast_uptime_ms=%lu\nrunning=%d\n",
                     (unsigned long)s_boot_count,
                     (unsigned long)uptime_ms(),
                     running);
    UINT bw;
    if (n > 0) f_write(&f, buf, (UINT)n, &bw);
    f_close(&f);
    diag_clear();
    readahead_invalidate();  /* host log dosyalarini yeniden okuyabilir */
}

/* --- state.txt oku (onceki oturum). Dosya yoksa false. --- */
static bool read_state(uint32_t *prev_boot, uint32_t *prev_uptime, int *prev_running) {
    *prev_boot = 0; *prev_uptime = 0; *prev_running = 0;
    diag_mark(DIAG_ST_LOGIO);
    FIL f;
    if (f_open(&f, LOG_STATE, FA_READ) != FR_OK) { diag_clear(); return false; }
    char buf[256];   /* basliktaki yorum satirlari + anahtarlar sigmali */
    UINT br = 0;
    f_read(&f, buf, sizeof(buf) - 1, &br);
    f_close(&f);
    diag_clear();
    buf[br] = '\0';

    const char *p;
    if ((p = strstr(buf, "boot_count=")))    *prev_boot    = (uint32_t)strtoul(p + 11, NULL, 10);
    if ((p = strstr(buf, "last_uptime_ms=")))*prev_uptime  = (uint32_t)strtoul(p + 15, NULL, 10);
    if ((p = strstr(buf, "running=")))       *prev_running = (int)strtoul(p + 8, NULL, 10);
    return true;
}

/* --- Dairesel rotasyon: events.log tavani astiysa .old'a devret ---
 * Cagiran, dosyayi KAPATTIKTAN sonra son boyutla cagirir. Rename/unlink
 * FAT'ta ucuz islemlerdir; hata olursa bir sonraki eklemede yeniden
 * denenir (en kotu durumda dosya tavanin bir satir ustunde kalir). */
static void rotate_if_needed(FSIZE_t size) {
    if (size < LOG_ROTATE_BYTES) return;
    f_unlink(LOG_EVENTS_OLD);              /* yoksa FR_NO_FILE: yok say */
    f_rename(LOG_EVENTS, LOG_EVENTS_OLD);
}

/* --- Bir olay satirini events.log'a ekle (yoksa RAM tamponuna al) --- */
static void append_line(const char *msg) {
    char line[LINE_MAX];
    snprintf(line, sizeof(line), "[t+%lus | boot#%lu] %s\r\n",
             (unsigned long)(uptime_ms() / 1000),
             (unsigned long)s_boot_count,
             msg);

    if (!s_mounted) {
        if (s_pend_count < PEND_MAX) {
            snprintf(s_pending[s_pend_count], LINE_MAX, "%s", line);
            s_pend_count++;
        }
        return;
    }

    diag_mark(DIAG_ST_LOGIO);
    FIL f;
    FSIZE_t sz = 0;
    if (f_open(&f, LOG_EVENTS, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
        UINT bw;
        f_write(&f, line, (UINT)strlen(line), &bw);
        sz = f_size(&f);
        f_close(&f);
    }
    rotate_if_needed(sz);
    diag_clear();
    readahead_invalidate();
}

/* --- Baglaninca bekleyen satirlari bosalt --- */
static void flush_pending(void) {
    if (!s_mounted || s_pend_count == 0) return;
    diag_mark(DIAG_ST_LOGIO);
    FIL f;
    FSIZE_t sz = 0;
    if (f_open(&f, LOG_EVENTS, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
        UINT bw;
        for (int i = 0; i < s_pend_count; i++) {
            f_write(&f, s_pending[i], (UINT)strlen(s_pending[i]), &bw);
        }
        sz = f_size(&f);
        f_close(&f);
        s_pend_count = 0;
    }
    rotate_if_needed(sz);
    diag_clear();
    readahead_invalidate();
}

void logger_mount(void) {
    if (s_mounted) return;
    diag_mark(DIAG_ST_MOUNT);
    FRESULT fr = f_mount(&s_fs, "0:", 1);
    diag_clear();
    if (fr == FR_OK) {
        s_mounted = true;
        diag_mark(DIAG_ST_LOGIO);
        f_mkdir(LOG_DIR);   /* zaten varsa FR_EXIST -> yok say */
        diag_clear();
        flush_pending();
    }
}

void logger_unmount(void) {
    if (!s_mounted) return;
    f_unmount("0:");
    s_mounted = false;
}

bool logger_init(void) {
    logger_mount();
    if (!s_mounted) return false;

    uint32_t pb = 0, pu = 0;
    int pr = 0;
    bool existed = read_state(&pb, &pu, &pr);

    s_boot_count = pb + 1;

    char msg[LINE_MAX];
    if (!existed) {
        snprintf(msg, sizeof(msg),
                 "=== ILK ACILIS === boot#%lu | mod: %s",
                 (unsigned long)s_boot_count, mode_str(MODE_READ));
    } else {
        snprintf(msg, sizeof(msg),
                 "=== ENERJILENDIRME === boot#%lu | onceki oturum ~%lu sn calisti (%s) | mod: %s",
                 (unsigned long)s_boot_count,
                 (unsigned long)(pu / 1000),
                 pr ? "enerji kesildi" : "durum bilinmiyor",
                 mode_str(MODE_READ));
    }
    append_line(msg);
    write_state(1);
    s_last_heartbeat_ms = uptime_ms();
    return true;
}

void logger_task(app_mode_t mode) {
    if (mode != MODE_READ || !s_mounted) return;
    uint32_t now = uptime_ms();
    if (now - s_last_heartbeat_ms >= HEARTBEAT_PERIOD_MS) {
        write_state(1);
        s_last_heartbeat_ms = now;
    }
}

void logger_log_button(app_mode_t from, app_mode_t to) {
    char msg[LINE_MAX];
    snprintf(msg, sizeof(msg), "BUTON: mod %s -> %s", mode_str(from), mode_str(to));
    append_line(msg);
}

void logger_log(const char *msg) {
    append_line(msg);
}
