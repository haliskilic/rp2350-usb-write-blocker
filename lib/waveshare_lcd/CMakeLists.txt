# Waveshare RP2350-GEEK 1.14" LCD (ST7789) kutuphanesi
# (Waveshare resmi demo kodundan uyarlanmistir)
add_library(waveshare_lcd STATIC
    Config/DEV_Config.c
    LCD/LCD_1in14_V2.c
    GUI/GUI_Paint.c
    Fonts/font8.c
    Fonts/font12.c
    Fonts/font16.c
    Fonts/font20.c
    Fonts/font24.c
    Fonts/font12CN.c
    Fonts/font24CN.c
)

target_include_directories(waveshare_lcd PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/Config
    ${CMAKE_CURRENT_LIST_DIR}/LCD
    ${CMAKE_CURRENT_LIST_DIR}/GUI
    ${CMAKE_CURRENT_LIST_DIR}/Fonts
)

target_link_libraries(waveshare_lcd PUBLIC
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_pwm
)

# Waveshare demo kodu -Wall ile bazi uyarilar uretir; kutuphaneyi hatalara
# cevirme (kendi kodumuz sikilastirilmistir).
target_compile_options(waveshare_lcd PRIVATE -w)
