# Tasarım Notları

🇹🇷 Türkçe | [🇬🇧 English](DESIGN.en.md)

## Amaç
RP2350-GEEK'i, takılı microSD kartı FAT32/exFAT dosya sistemiyle bir USB Mass
Storage aygıtı olarak sunan; varsayılan salt‑okunur, butonla oku/yaz arasında
geçiş yapan ve olayları FAT içindeki `logs/` klasörüne kaydeden bir firmware.

## Mimari kararlar

### Çekirdek modeli: core0 kooperatif döngü + core1 G/Ç motoru
Core0 ana döngüsü sırayla `tud_task()` (USB), `button_task()`, `mode_task()`,
`logger_task()` çağırır. Core1, SD ön-okuma/arkadan-yazma motorudur
(`readahead.c`): SPSC halkalar, `__dmb` yayın protokolü ve epoch
geçersizlemesiyle kilitsiz haberleşir; SD sürücü seviyesindeki eşzamanlılık
carlk3'ün kart-mutex'iyle (`sd_lock`) sıralanır. LCD spi1'de, SD SDIO 4-bit
(pio1) üzerindedir — donanım blokları çakışmaz.

### MSC blok erişimi = FatFs `disk_*` katmanı
`tud_msc_read10_cb` / `tud_msc_write10_cb`, core1 motoru üzerinden FatFs
`diskio` katmanına (`disk_read` / `disk_write`, pdrv=0) iner. Cihaz tarafı
loglama da aynı karta FatFs `f_*` ile erişir; ikisi de aynı carlk3 SD
sürücüsünde buluşur.

### Host ⇄ cihaz FAT tutarlılığı (kritik)
İki "master"ın aynı FAT'e eşzamanlı yazması bozulmaya yol açar. Çözüm: her an
FAT'e yalnızca **tek taraf** yazar.

| Mod | Host | Cihaz (log) |
|-----|------|-------------|
| OKU (varsayılan) | salt‑okunur (yazma reddedilir) | FS sahibi — serbest yazar |
| YAZ | yazma sahibi | FatFs bırakılır; loglar RAM'e tamponlanır |

Mod geçişinde medya kısa süre host gözünde "çıkarılır" (`test_unit_ready=false`,
sonra `true` + `UNIT ATTENTION`). Host bunu medya değişimi olarak algılar;
güncel FAT tablosunu ve yazma‑koruma bitini yeniden okur. Böylece OKU→YAZ
geçişinde host taze FAT ile başlar.

OKU modunda cihaz log yazarken host'un önbelleği bayatlayabilir; ancak host
salt‑okunur olduğu için **bozulma olmaz** (yalnızca kozmetik). Kullanıcı güncel
logları görmek isterse YAZ'a geçip dönmesi (ya da yeniden takması) yeterlidir.

### Buton = BOOTSEL
Kartta ayrı kullanıcı butonu yoktur. BOOT butonu, flash CS hattı üzerinden
çalışma sırasında okunur (`button.c`). Bu okuma açılışta değil çalışma anında
yapıldığından bootloader'a girilmez. Debounce: 40 ms.

### Güç/oturum loglama
RTC yok. `logs/state.txt` her ~10 sn'de bir güncellenir (boot sayacı, son
uptime, `running=1`). Açılışta bu dosya okunur:
- yoksa → "ILK ACILIS"
- varsa → önceki `running=1` kaldığından ani güç kesintisi çıkarımı yapılır ve
  "onceki oturum ~X sn calisti (enerji kesildi)" satırı düşülür.

Böylece "her enerjilendirme + önceki oturum süresi + her buton basımı" gereksinimi
karşılanır.

## Bilinen sınırlamalar / geliştirme yönleri
- Zaman damgaları görecelidir (uptime + boot sayacı); RTC/harici saat eklenebilir.
- Güç kesintisi süresi çözünürlüğü heartbeat periyoduna (~10 sn) bağlıdır.
- SD, SPI modunda 25 MHz (spesifikasyon üst sınırı) sürülür. Çekirdek-1
  ön-okuma motoru (`readahead.c`, 8×16 KB SRAM halkası) SD okumasını USB
  gönderimiyle örtüştürür: sıralı okuma **~1,0 MB/s** — full-speed USB'nin
  (12 Mbit/s) pratik tavanı. Gelişim: 542 KB/s (ilk sürüm) → 716 (25 MHz
  SPI) → 1000 KB/s (ön-okuma). Rastgele 4 KB gecikmesi ~29 ms (ön-okuma
  mutex'iyle paylaşımdan; kabul edilir). Tutarlılık: her yazma ve mod
  geçişi halkayı epoch ile geçersiz kılar; BOOTSEL buton okuması sırasında
  çekirdek-1 `multicore_lockout` ile RAM'e parklatılır (flash erişimi
  kapanırken çekirdek-1 flash'tan kod çekerse çöker).
- İlk erişim algısı: takma sonrası cihaz ~1 sn'de numaralanır, ilk SCSI
  isteğinde SD kart init edilir (~0,5 sn) ve Linux soğuk mount+df ~1,7 sn
  sürer (FSInfo sayesinde FAT tamamı okunmaz). Sonrası OS önbelleğinden
  hızlıdır; her OKU↔YAZ geçişi medyayı yeniden taktığı için ilk-erişim
  maliyeti tekrarlanır (tutarlılık tasarımının bedeli).
- OKU modunda cihaz yazımından sonra host görünümü, yeniden takana kadar bayat
  kalabilir (kozmetik).

## Saha doğrulaması (2026-07-19)

Gerçek donanımda (RP2350-GEEK + 64GB microSD) uçtan uca doğrulandı:

1. **Enumerasyon**: `cafe:4001 RP2350-GEEK Depolama`, 59,5 GiB
   tek LUN; OKU modunda kernel `Write Protect is on` raporlar.
2. **Okuma**: ham blok + FAT32 mount + dosya okuma sorunsuz.
3. **Yazma reddi (OKU)**: `dd` yazma denemesi "Salt-okunur dosya sistemi"
   ile reddedildi; okuma etkilenmedi.
4. **Mod geçişi**: buton kısa basımıyla YAZ(RW)'ye geçiş; host tarafında
   yazma açıldı; kart YAZ modunda USB üzerinden formatlandı (mkfs.fat).
5. **Loglama**: OKU'ya dönüşte cihaz `logs/events.log`'a
   `[t+1368s | boot#1] BUTON: mod YAZ(RW) -> OKU(R)` satırını ve
   `state.txt`'e boot sayacı/uptime'ı yazdı; host bunları okudu.

### Bulunan ve düzeltilen vendor hataları
- `LCD_1in14_V2.c`: `LCD_1IN14_V2_Clear` 64.800 baytlık VLA'yı yığında
  açıyordu (yığın 8 KB) → statik satır tamponuyla değiştirildi.
- `GUI_Paint.c`: scale-65 `Paint_Clear` X döngüsü bayt sayısı üzerinden
  dönüp her karede tamponun 480 bayt ötesine yazıyordu (logger durumunu
  eziyordu) → piksel sayısıyla sınırlandı.
- `crash.c` (carlk3): fault işleyicisinde koşulsuz `__breakpoint()`
  debugger yokken ARM LOCKUP'a yol açıyordu → `NVIC_SystemReset()`.
- `sd_card_spi.c` (carlk3): CRC hatasında TX/RX DMA çifti çalışır halde
  bırakılıyordu (sonraki zaman aşımsız SPI okumasını kilitleyebilir) →
  dönüşten önce `sd_spi_transfer_wait_complete`.

### SDIO 4-bit + arkadan-yazma (v13-v14 saha notları)
- SD artık PIO tabanlı SDIO 4-bit @ 18,75 MHz (pio1). PIO bölücüsü
  TAMSAYI olmalı: 25 MHz (böl=1,5) ile kart init hiç başarılamadı.
- **Kritik operasyonel not:** SD kart bir kez SPI moduna girerse yalnızca
  GÜÇ KESİLMESİ ile çıkar. SPI'lı eski firmware'den SDIO'lu firmware'e
  geçişte cihaz USB'den fiziksel çıkarılıp takılmalıdır (warm reboot
  yetmez — sahada bir tam hata ayıklama turuna mal oldu).
- Arkadan-yazma (4×16 KB halka): USB alımı ile SD yazımı örtüşür.
  Ölçümler: kararlı-durum yazma 928-941 KB/s (taban 683), okuma 1,0 MB/s
  korunur, rastgele 4 KB 29→18 ms. Bütünlük SHA-256 ile iki kez birebir
  doğrulandı. Soğuk ilk yazma (taze mount + masaüstü servisleri diski
  kurcalarken) 444 KB/s'ye düşebilir; çakışan-aralık drain düzeltmesiyle
  (365→444) firmware tarafı giderildi, kalan fark ortam gürültüsü + SD
  kartın ilk-dokunuş silme davranışı.
- Elektrik kesintisi penceresi: halkada en fazla 64 KB onaylanmış-ama-
  yazılmamış veri olabilir; SCSI SYNC/eject ve mod geçişi drain eder
  (güvenli çıkar = güvenli).

### Hızlı ardışık basışlar (saha notu)
Basışlar arası süre ~5 sn'den kısaysa host, ara mod geçişlerini tek bir
medya değişimi olarak birleştirebilir (2,5 sn çıkarma penceresi + ~2 sn
host yoklaması). Cihaz her basışı ayrı ayrı loglar ve host her durumda
NİHAİ moda doğru şekilde yakınsar; tutarlılık etkilenmez.

### Takılış gecikmesi vakası (v15-v17 saha kayıtları)
Belirti: takılıştan masaüstü bağlamasına 40+ sn (çekirdek diski 1,8 sn'de
görüyordu; gecikme udev'in blkid imza taramasındaydı).

Kök neden zinciri (strace ile ofset bazında kanıtlandı):
1. blkid GPT yedeğini disk SONUNDA arar;
2. oradaki sıralı-benzeri okumalar prefetch steering'ini tetikler;
3. core1 kart kapasitesinin ÖTESİNİ okumaya kalkar → SDIO sürücüsünün
   saniyeler süren zaman aşımı/yeniden deneme merdiveni → sd_lock
   tutulur → sıradaki host okuması 1-5 sn bekler.

Düzeltmeler: (a) prefetch'e kapasite sınırı (asıl çözüm), (b) steering
için ≥2 ardışık devam eşiği (dağınık taramalarda churn önlenir),
(c) host'ta cihaza özel udev kuralı `59-rp2350-geek.rules`
(read_ahead_kb=32; imza taramasının öndeleme trafiğini küçültür).

Sonuç: blkid 10,2→1,2 sn; takılış→masaüstü bağlama 42,7→2,8 sn (15×).
Sıralı 1,0 MB/s ve yazma ~930 KB/s etkilenmedi.
