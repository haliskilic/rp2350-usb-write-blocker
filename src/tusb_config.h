/*
 * tusb_config.h - TinyUSB yapilandirmasi
 *
 * Cihaz yalnizca tek bir USB Mass Storage (MSC) arayuzu sunar.
 * (CDC/HID vb. kapali -> host'ta yalnizca bir "removable disk" gorunur.)
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* -------- MCU / port -------- */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_RP2040   /* RP2350 icin de RP2040 driver'i kullanilir */
#endif

#define CFG_TUSB_OS           OPT_OS_PICO

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* -------- Bellek hizalama/bolge -------- */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

/* -------- Etkin sinif surucileri -------- */
#define CFG_TUD_CDC    0
#define CFG_TUD_MSC    1
#define CFG_TUD_HID    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

/* MSC FIFO buffer boyutu (bir seferde islenen blok tamponu).
 * 16 KB = 32 sektorluk zincirler -> daha az geri cagirma turu,
 * SD'ye daha buyuk coklu-blok okuma/yazmalar. RAM bol (~450 KB bos). */
#define CFG_TUD_MSC_EP_BUFSIZE 16384

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
