/*
 * config.h - RP2350-GEEK USB Mass Storage (OKU/YAZ) firmware
 *
 * Merkezi yapılandırma: pinler, mod tanımları, zamanlama sabitleri.
 *
 * Donanım (Waveshare RP2350-GEEK):
 *   LCD (ST7789, 240x135) -> spi1  : CLK=10 MOSI=11 CS=9 DC=8 RST=12 BL=13
 *   microSD (SPI modu)    -> spi0  : SCK=18 MOSI=19 MISO=20 CS=23
 *   Buton                 -> BOOTSEL (kart üzerindeki tek buton)
 *
 * LCD pinleri lib/waveshare_lcd/Config/DEV_Config.h içinde tanımlıdır.
 * SD pinleri src/hw_config.c içinde tanımlıdır.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* -------- Çalışma modları -------- */
typedef enum {
    MODE_READ = 0,  /* Salt okunur  -> ekranda "OKU (R)"  (varsayılan) */
    MODE_RW   = 1   /* Oku/Yaz      -> ekranda "YAZ (RW)"              */
} app_mode_t;

/* -------- Zamanlama -------- */
#define HEARTBEAT_PERIOD_MS   10000u  /* state.txt guncelleme periyodu (guc kesintisi cozunurlugu) */
#define BUTTON_DEBOUNCE_MS       40u  /* buton debounce suresi */
#define BUTTON_POLL_MS           10u  /* buton okuma periyodu */
/* Mod gecisinde medyanin "cikarilmis" gorunme suresi. Host'lar
 * cikarilabilir medyayi ~2 sn'de bir yoklar (Linux disk_events);
 * pencere bundan KISA olursa cikarma hic fark edilmez ve host eski
 * yazma-koruma durumuyla calismaya devam eder. 2,5 sn en az bir
 * yoklamanin pencereye denk gelmesini garantiler. */
#define EJECT_SETTLE_MS        2500u

/* -------- Log dosya yolları (FatFs, surucu "0:") -------- */
#define LOG_DIR         "0:/logs"
#define LOG_EVENTS      "0:/logs/events.log"
#define LOG_EVENTS_OLD  "0:/logs/events.old.log"
#define LOG_STATE       "0:/logs/state.txt"

/* Dairesel log tavani: events.log bu boyutu asinca events.old.log'a
 * devrilir (eskisi silinir). Iki dosya toplami ~1000 MB ile sinirlanir
 * -> loglar SD karti asla 1 GB'in uzerinde sisiremez. */
#define LOG_ROTATE_BYTES (500u * 1024u * 1024u)

/* state.txt basina yazilan kimlik yorumlari (proje baglantisi + iletisim).
 * ASCII tutulur; read_state() anahtar aramasi (strstr) yorumlardan
 * etkilenmez. */
#define PROJECT_URL     "https://github.com/haliskilic/rp2350-usb-write-blocker"
#define PROJECT_CONTACT "mail@haliskilic.com.tr"

#endif /* APP_CONFIG_H */
