# Design Notes

[🇹🇷 Türkçe](DESIGN.md) | 🇬🇧 English

## Goal
Firmware that presents the RP2350-GEEK's attached microSD card as a USB Mass
Storage device with a FAT32/exFAT filesystem; read-only by default, toggled
between read/write with the on-board button, and logging all events into the
`logs/` folder inside the FAT volume.

## Architectural decisions

### Core model: core0 cooperative loop + core1 I/O engine
Core0's main loop calls `tud_task()` (USB), `button_task()`, `mode_task()`
and `logger_task()` in sequence. Core1 is the SD read-ahead / write-behind
engine (`readahead.c`): it communicates lock-free via SPSC rings, a `__dmb`
publish protocol and epoch invalidation; concurrency at the SD-driver level
is serialized by carlk3's per-card mutex (`sd_lock`). The LCD lives on spi1
and the SD on SDIO 4-bit (pio1) — the hardware blocks never conflict.

### MSC block access = the FatFs `disk_*` layer
`tud_msc_read10_cb` / `tud_msc_write10_cb` reach the FatFs `diskio` layer
(`disk_read` / `disk_write`, pdrv=0) through the core1 engine. Device-side
logging accesses the same card via FatFs `f_*`; both meet in the same carlk3
SD driver.

### Host ⇄ device FAT coherence (critical)
Two "masters" writing the same FAT concurrently corrupts it. The solution:
at any moment only **one side** writes.

| Mode | Host | Device (logging) |
|------|------|------------------|
| READ (default) | read-only (writes rejected) | owns the FS — writes freely |
| WRITE | write owner | FatFs unmounted; log lines buffered in RAM |

On every mode toggle the media is briefly "ejected" in the host's eyes
(`test_unit_ready=false`, then `true` + `UNIT ATTENTION`). The host treats
this as a media change and re-reads the FAT tables and the write-protect
bit, so a READ→WRITE transition always starts from a fresh FAT.

While the device writes logs in READ mode the host's cache may go stale;
since the host is read-only this causes **no corruption** (cosmetic only).
To see fresh logs, toggle to WRITE and back (or replug).

### Button = BOOTSEL
The board has no dedicated user button. The BOOT button is sampled at
runtime through the flash chip-select line (`button.c`); because sampling
happens during execution, not at reset, it never enters the bootloader.
Debounce: 40 ms. A short press toggles the mode; holding ≥5 s reboots into
the USB bootloader (BOOTSEL) for easy UF2 updates. While the flash CS line
is toggled, core1 is parked in RAM via `multicore_lockout` (fetching code
from flash at that instant would crash it).

### Power/session logging
There is no RTC. `logs/state.txt` is rewritten every ~10 s (boot counter,
current uptime, `running=1`). At boot this file is read:
- absent → "FIRST BOOT"
- present → the leftover `running=1` implies sudden power loss, and a
  "previous session ran ~X s (power lost)" line is appended.

This satisfies "log every power-up + previous session duration + every
button press".

## Known limitations / future work
- Timestamps are relative (uptime + boot counter); an RTC could be added.
- Power-loss duration resolution equals the heartbeat period (~10 s), and
  the heartbeat only runs in READ mode — time spent in WRITE mode freezes
  the measurement at the last READ heartbeat.
- The core1 read-ahead engine (`readahead.c`, 8×16 KB SRAM ring) overlaps
  SD reads with USB transmission: sequential reads reach **~1.0 MB/s** —
  the practical ceiling of full-speed USB (12 Mbit/s). Progression:
  542 KB/s (first version) → 716 (25 MHz SPI) → 1000 KB/s (read-ahead).
  Consistency: every write and every mode toggle invalidates the ring via
  an epoch counter.
- First-access perception: after plugging, the device enumerates in ~1 s,
  the SD card is initialized on the first SCSI request (~0.5 s), and a cold
  Linux mount+df takes ~1.7 s (FSInfo spares reading the whole FAT).
  Everything afterwards is served from the OS cache; each READ↔WRITE toggle
  re-inserts the media, so the first-access cost repeats (the price of the
  coherence design).
- After device-side writes in READ mode, the host's view may stay stale
  until re-insertion (cosmetic).

## Field verification (2026-07-19)

Verified end-to-end on real hardware (RP2350-GEEK + 64 GB microSD):

1. **Enumeration**: `cafe:4001`, 59.5 GiB single LUN; in READ mode the
   kernel reports `Write Protect is on`.
2. **Reads**: raw block + FAT32 mount + file reads all clean.
3. **Write rejection (READ)**: `dd` write attempts rejected with
   "read-only file system"; reads unaffected.
4. **Mode toggle**: short press switches to WRITE(RW); host-side writing
   enabled; the card was even formatted over USB while in WRITE mode.
5. **Logging**: on return to READ the device appended the button event to
   `logs/events.log` and updated boot counter/uptime in `state.txt`; the
   host read both back.

### Vendor bugs found and patched
- `LCD_1in14_V2.c`: `LCD_1IN14_V2_Clear` allocated a 64,800-byte VLA on an
  8 KB stack → replaced with a static single-row buffer.
- `GUI_Paint.c`: the 65K-color `Paint_Clear` X loop iterated over the byte
  count instead of the pixel count, overrunning the buffer by 480 bytes
  every frame (trampling logger state) → bounded by pixel count.
- `crash.c` (carlk3): the fault handler executed an unconditional
  `__breakpoint()`; without a debugger that is an ARM LOCKUP →
  `NVIC_SystemReset()`.
- `sd_card_spi.c` (carlk3): on a CRC error the TX/RX DMA pair was left
  running (could wedge the next timeout-less SPI read forever) →
  `sd_spi_transfer_wait_complete` before returning.

### SDIO 4-bit + write-behind (v13-v14 field notes)
- SD now runs PIO-based SDIO 4-bit @ 18.75 MHz (pio1). The PIO divider must
  be an INTEGER: at 25 MHz (div=1.5) card init never succeeded.
- **Critical operational note:** once an SD card enters SPI mode it can only
  leave it via a POWER CYCLE. When moving from an SPI-based firmware to the
  SDIO one, the device must be physically unplugged and replugged (a warm
  reboot is not enough — this cost a full debugging round in the field).
- Write-behind (4×16 KB ring): USB reception overlaps SD programming.
  Measurements: steady-state writes 928-941 KB/s (baseline 683), reads stay
  at 1.0 MB/s, random 4 KB 29→18 ms. Integrity verified twice with SHA-256,
  bit-exact. A cold first write (fresh mount while desktop services poke the
  disk) can dip to ~444 KB/s; the firmware-side cause was fixed with the
  overlapping-range drain (365→444), the remainder is environment noise plus
  the SD card's first-touch erase behaviour.
- Power-loss window: at most 64 KB of acknowledged-but-unwritten data can
  sit in the ring; SCSI SYNC/eject and mode toggles drain it
  (safe-eject = safe).

### Rapid consecutive presses (field note)
If presses are less than ~5 s apart the host may coalesce intermediate mode
transitions into a single media change (2.5 s eject window + ~2 s host
polling). The device logs every press individually and the host always
converges to the correct FINAL mode; coherence is unaffected.

### The plug-in latency case (v15-v17 field logs)
Symptom: 40+ s from plug-in to desktop mount (the kernel saw the disk in
1.8 s; the delay was in udev's blkid signature scan).

Root-cause chain (proven offset-by-offset with strace):
1. blkid probes the GPT backup at the END of the disk;
2. the sequential-looking reads there trigger prefetch steering;
3. core1 attempts to read PAST the card's capacity → the SDIO driver's
   multi-second timeout/retry ladder → `sd_lock` held → the next host read
   stalls 1-5 s.

Fixes: (a) a capacity clamp on the prefetcher (the real cure), (b) a ≥2
consecutive-continuation threshold for steering (kills churn on scattered
scans), (c) a device-specific udev rule on the host,
`59-rp2350-geek.rules` (read_ahead_kb=32, shrinking the probe's readahead
traffic).

Result: blkid 10.2→1.2 s; plug-in→desktop mount 42.7→2.8 s (15×).
Sequential 1.0 MB/s and ~930 KB/s writes unaffected.
