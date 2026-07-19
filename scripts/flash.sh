#!/usr/bin/env bash
# Firmware'i karta yukler.
#
# Kart BOOTSEL modunda olmali:
#   BOOT butonuna basili tutarken RESET'e bir kez bas, once RESET'i birak,
#   sonra BOOT'u birak. Bilgisayarda "RP2350" adli bir surucu belirir.
#
# Bu betik once picotool'u dener (USB destegi varsa), olmazsa BOOTSEL
# surucusunu bulup .uf2 dosyasini kopyalar.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UF2="$ROOT/build/usbwriteblocker.uf2"
PICOTOOL="$ROOT/build/_deps/picotool/picotool"

[ -f "$UF2" ] || { echo "[flash] $UF2 yok. Once scripts/build.sh calistirin." >&2; exit 1; }

# 1) picotool (USB destekli derlenmisse)
if [ -x "$PICOTOOL" ] && "$PICOTOOL" help load >/dev/null 2>&1; then
    if "$PICOTOOL" info >/dev/null 2>&1; then
        echo "[flash] picotool ile yukleniyor..."
        "$PICOTOOL" load -x "$UF2" && exit 0 || true
    fi
fi

# 2) BOOTSEL kutlesel depolama surucusu (RP2350 / RPI-RP2)
for label in RP2350 RPI-RP2; do
    MP=$(lsblk -o LABEL,MOUNTPOINT -nr 2>/dev/null | awk -v l="$label" '$1==l {print $2}')
    if [ -n "${MP:-}" ]; then
        echo "[flash] BOOTSEL surucusu bulundu: $MP -> uf2 kopyalaniyor..."
        cp "$UF2" "$MP/" && sync
        echo "[flash] Tamam. Kart yeniden baslayacak."
        exit 0
    fi
done

echo "[flash] BOOTSEL cihazi/surucusu bulunamadi." >&2
echo "        Karti BOOTSEL moduna alip tekrar deneyin (BOOT basili + RESET)." >&2
exit 1
