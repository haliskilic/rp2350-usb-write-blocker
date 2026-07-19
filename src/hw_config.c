/*
 * hw_config.c - carlk3 no-OS-FatFS-SD SD kart donanim yapilandirmasi
 *
 * RP2350-GEEK microSD yuvasi SDIO 4-bit icin kablolanmistir (sematikte
 * SDIO_SCK/SDIO_CMD adlariyla):
 *   CLK = GP18  (D0 - 2 kurali: SDIO_CLK_PIN_D0_OFFSET)
 *   CMD = GP19
 *   D0..D3 = GP20..GP23 (ardisik olmak zorunda)
 *
 * PIO tabanli SDIO surucusu (ZuluSCSI kokenli) kullanilir: pio1 + DMA.
 * 4-bit @ 25 MHz -> SD tarafi ~10 MB/s (USB tavaninin cok ustunde);
 * kazanim rastgele gecikme ve yazma tarafindadir.
 *
 * Not: Ayni pinlerle SPI modu da mumkundur (eski surumler icin git
 * gecmisine bakin); SDIO sorun cikarirsa donus kolaydir.
 */
#include "hw_config.h"

static sd_sdio_if_t sdio_if = {
    .CMD_gpio = 19,
    .D0_gpio  = 20,
    .SDIO_PIO = pio1,          /* DIKKAT: carlk3 surucusu pin muksunu
                                * GPIO_FUNC_PIO1 olarak SABITLER
                                * (sd_card_sdio.c/gpio_conf) — bu alan
                                * pio1'den farkli secilirse pinler yanlis
                                * mukslenir ve kart hic gelmez. */
    .DMA_IRQ_num = DMA_IRQ_1,
    /* clk_sys 150 MHz: PIO bolucusu TAMSAYI olmali (kesirli bolucu
     * kenar jitteri yaratir ve kart init'i bozar — sahada dogrulandi:
     * 150/6=25 MHz, clk_div=1,5 ile kart hic gelmedi).
     * 150/8 = 18,75 MHz (clk_div=2,0) -> 4-bit'te ~9 MB/s. */
    .baud_rate = 150 * 1000 * 1000 / 8
};

/* Tek SD kart nesnesi (yuva) */
static sd_card_t sd_card = {
    .type = SD_IF_SDIO,
    .sdio_if_p = &sdio_if,
    .use_card_detect = false  /* GEEK yuvasinda CD hatti kullanilmiyor */
};

size_t sd_get_num(void) { return 1; }

sd_card_t *sd_get_by_num(size_t num) {
    if (0 == num) {
        return &sd_card;
    }
    return NULL;
}
