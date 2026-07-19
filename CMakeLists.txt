cmake_minimum_required(VERSION 3.13)

# --- RP2350 hedefi ---
# RP2350-GEEK, RP2350A cipini kullanir. Pico 2 board tanimi bu cip icin
# uygun platform/derleyici ayarlarini getirir.
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

# Pico SDK (proje()'den once dahil edilmeli)
include(pico_sdk_import.cmake)

project(usbwriteblocker C CXX ASM)

pico_sdk_init()

# --- Bagimliliklar ---
# carlk3 SD kart + FatFs kutuphanesi (INTERFACE hedefi):
add_subdirectory(lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src build_fatfs)
# Waveshare 1.14" LCD:
add_subdirectory(lib/waveshare_lcd)

# --- Uygulama ---
add_executable(${PROJECT_NAME}
    src/main.c
    src/msc_disk.c
    src/usb_descriptors.c
    src/hw_config.c
    src/mode.c
    src/logger.c
    src/button.c
    src/display.c
    src/diag.c
    src/readahead.c
)

# tusb_config.h ve uygulama basliklari src/ altinda
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_unique_id
    pico_multicore
    hardware_watchdog
    tinyusb_device
    tinyusb_board
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    waveshare_lcd
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
)

# FatFs (exFAT, FF_FS_TINY=0 -> her FIL 512B sektor tamponu icerir) cagri
# zinciri varsayilan 2KB yigina sigmaz. Yigini buyut + tasmayi yakalamak
# icin stack guard'lari ac.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_STACK_SIZE=0x2000
    # Cekirdek-1 yigini 4KB'lik SCRATCH_X'te yasar; core1 yalnizca blok
    # surucusu kosar (derin FatFs cagrilari core0'da) -> 4KB yeterli.
    PICO_CORE1_STACK_SIZE=0x1000
    PICO_USE_STACK_GUARDS=1
)

# Binary meta bilgisi (picotool info ile gorunur)
pico_set_program_name(${PROJECT_NAME} "RP2350 USB Write Blocker")
pico_set_program_version(${PROJECT_NAME} "1.0.0")

# USB'yi MSC icin kullaniyoruz; stdio'yu USB'ye baglamiyoruz.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# .uf2 / .elf / .bin ciktilarini uret
pico_add_extra_outputs(${PROJECT_NAME})
