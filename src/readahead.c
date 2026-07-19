/*
 * readahead.c - Cekirdek-1 SD on-okuma motoru (SPSC halka tampon)
 *
 * Uretici: cekirdek-1 (yalnizca disk_read). Tuketici: cekirdek-0 (MSC).
 * Yayin protokolu: tampon doldur -> __dmb() -> valid=true. Gecersiz
 * kilma: epoch sayaci; cekirdek-1 yayin oncesi epoch'u dogrular,
 * degistiyse urettigini coper.
 */
#include "readahead.h"
#include "diag.h"

#include "diskio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#include <string.h>

#include "tusb_config.h"

/* Parca boyutu MSC aktarim parcasiyla ayni olmali: her geri cagirma
 * tam bir slota denk gelir. */
#define RA_CHUNK_BYTES   CFG_TUD_MSC_EP_BUFSIZE
#define RA_CHUNK_SECTORS (RA_CHUNK_BYTES / 512u)
#define RA_SLOTS         8u   /* 8 x 16 KB = 128 KB SRAM */

typedef struct {
    uint32_t      lba;
    uint32_t      epoch;
    uint32_t      off;    /* tuketilen sektor sayisi (yalniz core0 yazar) */
    volatile bool valid;
} ra_slot_t;

#define RA_MIN(a, b) ((a) < (b) ? (a) : (b))

static uint8_t   ra_buf[RA_SLOTS][RA_CHUNK_BYTES];
static ra_slot_t ra_slot[RA_SLOTS];

static volatile uint32_t ra_epoch = 1;         /* invalidate ile artar */
static volatile uint32_t ra_capacity = 0;      /* kart sektor sayisi (core1 doldurur) */
static volatile uint32_t ra_stream_lba = 0;    /* cekirdek-1'in siradaki hedefi */
static volatile bool     ra_stream_on = false; /* akis aktif mi */
static volatile bool     ra_run = false;       /* cekirdek-1 calisiyor */

static uint32_t ra_cons = 0;                   /* tuketici indeksi (yalniz core0) */
static uint32_t ra_last_end = 0;               /* son istegin bitis LBA'si (core0) */
static uint32_t ra_seq_run  = 0;               /* ardisik devam sayaci (core0) */

/* -------- Arkadan-yazma halkasi (SPSC: core0 uretir, core1 tuketir) --
 * SDIO ile SD yazimi (~2 ms/16 KB) USB aliminin (~14 ms) cok altinda:
 * halka pratikte 1-2 slotu gecmez; 4 slot fazlasiyla yeterli. */
#define WB_SLOTS 4u

typedef struct {
    uint32_t      lba;
    uint32_t      nsec;
    volatile bool valid;
} wb_slot_t;

static uint8_t   wb_buf[WB_SLOTS][RA_CHUNK_BYTES];
static wb_slot_t wb_slot[WB_SLOTS];
static uint32_t  wb_prod = 0;                 /* yalniz core0 */
static volatile uint32_t wb_cons = 0;         /* yalniz core1 yazar */
static volatile bool wb_error = false;        /* kalici yazma hatasi */

bool readahead_running(void) { return ra_run; }

bool writebehind_pending(void) {
    for (uint32_t i = 0; i < WB_SLOTS; i++)
        if (wb_slot[i].valid) return true;
    return false;
}

/* Okunacak aralik, kuyruktaki bir yazmayla cakisiyor mu?
 * (core0 tek is parcacikli: kontrol sirasinda yeni kayit eklenemez;
 * core1'in slotu tam o an bosaltmasi yalnizca zararsiz yanlis-pozitif
 * uretir.) */
static bool wb_overlaps(uint32_t lba, uint32_t nsec) {
    for (uint32_t i = 0; i < WB_SLOTS; i++) {
        wb_slot_t *s = &wb_slot[i];
        if (s->valid &&
            lba < s->lba + s->nsec && s->lba < lba + nsec) return true;
    }
    return false;
}

bool writebehind_drain(void) {
    while (writebehind_pending() && !wb_error) {
        diag_feed();
        tight_loop_contents();
    }
    return !wb_error;
}

void writebehind_clear_error(void) {
    /* Hata durumunda core1 halkayi zaten bosaltmistir (slotlari hata
     * sonrasi da dusurur); bayragi temizlemek yeni oturum icin guvenli. */
    if (!writebehind_pending()) wb_error = false;
}

int32_t writebehind_enqueue(uint32_t lba, const void *data, uint32_t bufsize) {
    if (wb_error) return -1;
    if (!ra_run) {
        /* cekirdek-1 yoksa dogrudan senkron yaz */
        if (disk_write(0, (const BYTE *)data, lba, bufsize / 512u) != RES_OK)
            return -1;
        return (int32_t)bufsize;
    }
    wb_slot_t *s = &wb_slot[wb_prod];
    while (s->valid) {                        /* halka dolu: bosalmayi bekle */
        if (wb_error) return -1;
        diag_feed();
        tight_loop_contents();
    }
    memcpy(wb_buf[wb_prod], data, bufsize);
    s->lba  = lba;
    s->nsec = bufsize / 512u;
    __dmb();                                  /* veri gorunur olmadan valid deme */
    s->valid = true;
    wb_prod = (wb_prod + 1) % WB_SLOTS;
    return (int32_t)bufsize;
}

void readahead_invalidate(void) {
    ra_stream_on = false;
    ra_epoch++;                     /* cekirdek-1 bekleyen yayinlari coper */
    __dmb();
    for (uint32_t i = 0; i < RA_SLOTS; i++) ra_slot[i].valid = false;
    ra_cons = 0;
}

/* Akisi verilen LBA'dan yeniden kur (yalniz core0 cagirir). */
static void ra_steer(uint32_t lba) {
    ra_epoch++;
    __dmb();
    for (uint32_t i = 0; i < RA_SLOTS; i++) ra_slot[i].valid = false;
    ra_cons = 0;
    ra_stream_lba = lba;
    __dmb();
    ra_stream_on = true;
}

int32_t readahead_read(uint32_t lba, void *buffer, uint32_t bufsize) {
    uint32_t nsec = bufsize / 512u;
    uint32_t done = 0;   /* servis edilen sektor sayisi */

    /* Okuma-yazma sirasi tutarliligi: yalnizca CAKISAN aralik varsa
     * bekle. Cakismayan okumalar (or. FAT sektorleri) kuyruktaki veri
     * yazmalarini beklemeden servis edilir; aksi halde her FAT okumasi
     * tam drain tetikleyip yazma akisini serilestiriyordu (sahada
     * 365 KB/s'ye dusus olarak olculdu). */
    if (wb_overlaps(lba, nsec)) {
        if (!writebehind_drain()) return -1;
    }

    /* Host aktarimlari slot boyutunun tam kati degildir (or. 120 KB =
     * 7,5 slot); istekler slot sinirlarina gore kayik gelebilir ve tek
     * istek iki slota yayilabilir. Slot ici ofset takibiyle sirali akis
     * hizalamadan bagimsiz kesintisiz servis edilir. */
    if (ra_run) {
        while (done < nsec) {
            ra_slot_t *s = &ra_slot[ra_cons];
            if (!(s->valid && s->epoch == ra_epoch)) break;
            if (s->off >= RA_CHUNK_SECTORS) {           /* slot bitti */
                s->valid = false;
                ra_cons = (ra_cons + 1) % RA_SLOTS;
                continue;
            }
            if (s->lba + s->off != lba + done) break;   /* hizada degil */
            __dmb();
            uint32_t take = RA_MIN(RA_CHUNK_SECTORS - s->off, nsec - done);
            memcpy((uint8_t *)buffer + (size_t)done * 512u,
                   ra_buf[ra_cons] + (size_t)s->off * 512u,
                   (size_t)take * 512u);
            s->off += take;
            done   += take;
            if (s->off == RA_CHUNK_SECTORS) {
                s->valid = false;
                ra_cons = (ra_cons + 1) % RA_SLOTS;
            }
        }
        if (done == nsec) {                             /* tam isabet */
            ra_last_end = lba + nsec;
            return (int32_t)bufsize;
        }
    }

    /* Iska (veya kismi iska): kalani dogrudan oku. */
    if (disk_read(0, (BYTE *)buffer + (size_t)done * 512u,
                  lba + done, nsec - done) != RES_OK) return -1;

    /* ARDIŞIKLIK SEZGISI: akisi ancak >=2 ardisik devamdan sonra
     * yonlendir. Boylece dagitik imza taramalari (blkid; host'un 32 KB
     * ondelemesiyle 2'ser parcalik pencereler) prefetch churn'u hic
     * tetiklemez; gercek akislar 3. parcadan itibaren ring hizindadir.
     * (Churn maliyeti sahada IO basina ~35-120 ms olarak olculmustu.) */
    if (lba == ra_last_end) {
        ra_seq_run++;
        if (ra_run && ra_seq_run >= 2) ra_steer(lba + nsec);
    } else {
        ra_seq_run = 0;
    }
    ra_last_end = lba + nsec;
    return (int32_t)bufsize;
}

/* ---------------- cekirdek-1 ---------------- */

static void core1_main(void) {
    /* Cekirdek-0 flash'i gecici devre disi birakirken (BOOTSEL okuma)
     * bizi RAM'de parklayabilsin. */
    multicore_lockout_victim_init();

    uint32_t my_epoch = 0;
    uint32_t prod = 0;

    while (true) {
        /* ONCELIK: bekleyen yazmalari bosalt (FIFO sirasiyla). */
        wb_slot_t *w = &wb_slot[wb_cons];
        if (w->valid) {
            __dmb();
            DRESULT dw = disk_write(0, (const BYTE *)wb_buf[wb_cons],
                                    w->lba, w->nsec);
            if (dw != RES_OK) wb_error = true;   /* kalici hata isareti */
            w->valid = false;
            wb_cons = (wb_cons + 1) % WB_SLOTS;
            continue;
        }

        if (!ra_stream_on) { sleep_us(200); continue; }

        uint32_t e = ra_epoch;
        if (e != my_epoch) { my_epoch = e; prod = 0; }

        ra_slot_t *s = &ra_slot[prod];
        if (s->valid) { sleep_us(100); continue; }  /* halka dolu */

        uint32_t L = ra_stream_lba;

        /* KAPASITE SINIRI: kart sonunun otesine on-okuma yapma. Sinir
         * disi SDIO okumasi surucunun saniyeler suren zaman-asimi
         * merdivenine girer ve sd_lock'u tutarak host okumalarini
         * 1-5 sn bekletir (blkid'nin disk sonundaki GPT taramasinda
         * sahada olculdu). */
        if (ra_capacity == 0) {
            uint64_t cap = 0;
            if (disk_ioctl(0, GET_SECTOR_COUNT, &cap) == RES_OK && cap > 0)
                ra_capacity = (uint32_t)cap;
        }
        if (ra_capacity != 0 && L + RA_CHUNK_SECTORS > ra_capacity) {
            ra_stream_on = false;   /* akis kart sonunda biter */
            continue;
        }
        DRESULT dr = disk_read(0, (BYTE *)ra_buf[prod], L, RA_CHUNK_SECTORS);

        if (ra_epoch != e) continue;  /* bu arada gecersiz kilindi: cope */

        if (dr == RES_OK) {
            s->lba = L;
            s->epoch = e;
            s->off = 0;
            __dmb();                  /* tampon gorunur olmadan valid deme */
            s->valid = true;
            prod = (prod + 1) % RA_SLOTS;
            ra_stream_lba = L + RA_CHUNK_SECTORS;
        } else {
            /* kart sonu / okuma hatasi: akisi durdur */
            ra_stream_on = false;
        }
    }
}

void readahead_start(void) {
    if (ra_run) return;
    multicore_launch_core1(core1_main);
    ra_run = true;
}
