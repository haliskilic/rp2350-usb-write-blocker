#!/usr/bin/env bash
# Pico SDK'yi (RP2350 destekli) external/ altina klonlar ve tinyusb
# submodule'unu getirir. Bir kez calistirmak yeterlidir.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_DIR="$ROOT/external/pico-sdk"
SDK_TAG="2.1.1"

if [ -d "$SDK_DIR/.git" ] || [ -f "$SDK_DIR/pico_sdk_init.cmake" ]; then
    echo "[setup] pico-sdk zaten mevcut: $SDK_DIR"
else
    echo "[setup] pico-sdk $SDK_TAG klonlaniyor..."
    git clone --depth 1 --branch "$SDK_TAG" https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
    echo "[setup] tinyusb submodule getiriliyor..."
    git -C "$SDK_DIR" submodule update --init --depth 1 lib/tinyusb
fi

echo "[setup] Tamamlandi. Derlemek icin: scripts/build.sh"
