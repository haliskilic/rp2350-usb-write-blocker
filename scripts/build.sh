#!/usr/bin/env bash
# Firmware'i derler. Cikti: build/usbwriteblocker.uf2
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PICO_SDK_PATH="$ROOT/external/pico-sdk"

if [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "[build] pico-sdk yok. Once scripts/setup.sh calistirin." >&2
    exit 1
fi

mkdir -p "$ROOT/build"
cd "$ROOT/build"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. >/dev/null
ninja

echo
echo "[build] Tamam: build/usbwriteblocker.uf2"
echo "[build] Yuklemek icin: scripts/flash.sh (kart BOOTSEL modunda olmali)"
