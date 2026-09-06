# DAND_IQ
# SX1255 Kontrol Konsolu — Kullanım ve Mimari Rehberi

**Firmware:** `main_v6_1.c`
**Hedef:** STM32 (Dand-IQ PMOD kartı) + SX1255 RF front-end
**Radyo modu:** Mode A (ham 1-bit IQ), 433 MHz, 36 MHz kristal
**Arayüz:** USART2 @ **115200 baud, 8N1**

Bu belge firmware'in nasıl çalıştığını, terminalden nasıl kontrol edeceğini ve nelere dikkat etmen gerektiğini adım adım anlatır.

---

## 1. Hızlı Başlangıç

1. `main_v6_1.c` içeriğini projenin `main.c`'sine yapıştır.
2. CubeMX'te **USART2 global interrupt**'ın açık olduğundan emin ol (bkz. §7 uyarılar).
3. Derle, karta yükle.
4. Bir seri terminal aç (PuTTY / TeraTerm / minicom / STM32CubeIDE Terminal), **115200 8N1**.
5. Kart açılınca şuna benzer bir açılış ekranı gelir:

```
########################################
 SX1255 KONSOL v6.1 - 433 MHz Mode A
 bring-up: OK (RX acik)
########################################
===== SX1255 KONSOL v6.1 =====
help / stat / mon on|off
rx on|off   tx on|off   pa on|off
...
==============================
>
```

`>` işareti komut beklediğini gösterir. Komutu yazıp **Enter**'a bas.

> **İpucu:** Terminalinde "local echo" kapalıysa yazdığın harfleri göremezsin ama komut yine çalışır. Görmek istersen terminal ayarından local echo'yu aç.

---

## 2. Sistem Nasıl Çalışıyor? (Genel Mimari)

Firmware üç katmandan oluşur:

**a) Donanım sürücü katmanı** — `sx_read()` / `sx_write()`
SX1255 ile SPI üzerinden konuşur. Her register erişimi 2 bayttır: adres baytı + veri baytı. Adres baytının en üst biti (MSB) yazma/okuma bitidir (`1`=yaz, `0`=oku). CS (chip select) pini yazılımla sürülür.

**b) Radyo kontrol katmanı** — `sx_set_freq()`, `sx_apply_mode()`, `sx_bringup()`
Register'ları anlamlı işlevlere çevirir: frekans ayarla, RX/TX aç-kapat, PLL kilidini bekle.

**c) Konsol katmanı** — `process_line()` + UART kesme geri çağırması
UART'tan gelen metni komutlara ayrıştırır ve ilgili radyo işlevini çağırır.

### Çalışma akışı (açılıştan itibaren)

```
main()
 ├─ HAL/clock/GPIO/SPI/UART init
 ├─ sx_manual_reset()      → SX1255'e temiz reset
 ├─ sx_bringup()           → XOSC başlat, config yaz, RX'i aç
 ├─ açılış mesajı + help + ">"
 ├─ HAL_UARTEx_ReceiveToIdle_IT()  → komut dinlemeyi başlat
 └─ sonsuz döngü:
      ├─ line_ready ise → process_line()  (komut işle)
      ├─ mon açıksa → saniyede bir durum bas
      └─ LED heartbeat (sağlıklıysa hızlı, değilse yavaş)
```

### `mode_shadow` — kalbin attığı yer

MODE register'ı (0x00) 4 kritik biti tutar:

| Bit | Ad | Anlamı |
|-----|-----|--------|
| 0 | `ref_enable` | XOSC + güç dağıtımı (her şeyin temeli) |
| 1 | `rx_enable` | RX zinciri (LNA, mixer, RX PLL, ADC) |
| 2 | `tx_enable` | TX zinciri (DAC, mixer, TX PLL) |
| 3 | `driver_enable` | PA sürücü (asıl RF çıkış gücü) |

Firmware bu register'ın bir **gölge kopyasını** (`mode_shadow` değişkeni) RAM'de tutar. Her komut ilgili biti değiştirip çipe basar. Böylece "şu an ne açık?" bilgisi hep tutarlıdır ve `stat` ile görülebilir.

### Neden bu tasarım güvenli?

Her mod değişikliğinden sonra `sx_apply_mode()` ilgili **PLL kilidini bekler** ve raporlar. Yani `tx on` dediğinde sadece biti set etmez, TX PLL'i gerçekten kilitlendi mi diye `STAT` register'ını yoklar ve sana `TX PLL locked` ya da `KILITLENMEDI` der. Kör uçuş yok.

---

## 3. Komut Referansı

### Durum ve Yardım

| Komut | Açıklama |
|-------|----------|
| `help` | Komut menüsünü gösterir |
| `stat` | XOSC/PLL/MODE durumunu okur ve basar |
| `mon on` | Saniyede bir otomatik durum akışı başlatır |
| `mon off` | Otomatik akışı durdurur |

**Örnek:**
```
> stat
STAT=0x06 XOSC=1 PLLrx=1 PLLtx=0 | MODE=0x03 TXFE1=0x2E VER=0x11
```
- `XOSC=1` → kristal salınıyor, sağlıklı
- `PLLrx=1` → RX PLL kilitli
- `PLLtx=0` → TX PLL kapalı (TX açık değil)
- `MODE=0x03` → ref+rx açık

### Güç / Mod Kontrolü

| Komut | Açıklama |
|-------|----------|
| `rx on` / `rx off` | RX zincirini aç/kapat |
| `tx on` / `tx off` | TX zincirini aç/kapat (kapanınca PA da kapanır) |
| `pa on` / `pa off` | PA sürücüyü (güç katı) aç/kapat |
| `standby` | Sadece XOSC açık, rx/tx kapalı (düşük güç) |
| `sleep` | Her şey kapalı (ref dahil) — en düşük güç |

**Örnek — RX'ten TX'e geçiş:**
```
> rx off
  MODE=0x01 [ref=1 rx=0 tx=0 pa=0]
> tx on
  MODE=0x05 [ref=1 rx=0 tx=1 pa=0]
  TX PLL locked
```

> **Not:** RX ve TX'i aynı anda açık bırakabilirsin (tam-dupleks). SX1255 half/full duplex destekler. Sadece "önce RX'i kapatmalıyım" zorunluluğu yok.

### Frekans

| Komut | Açıklama |
|-------|----------|
| `freq rx <MHz>` | RX taşıyıcı frekansı |
| `freq tx <MHz>` | TX taşıyıcı frekansı |
| `freq both <MHz>` | RX ve TX'i aynı frekansa |

**Örnekler:**
```
> freq rx 433.5
  RX freq = 433500000 Hz  (Frf=0xC0D5D8)
> freq tx 435
  TX freq = 435000000 Hz  (Frf=0xC29249)
> freq both 434.0
  RX freq = 434000000 Hz  (Frf=0xC11EB8)
  TX freq = 434000000 Hz  (Frf=0xC11EB8)
```

**Frekans nasıl hesaplanıyor?** Datasheet formülü:
```
Frf = frekans(Hz) × 2²⁰ / F_XOSC
    = frekans(Hz) × 1048576 / 36000000
```
Bu 24-bit değer üç register'a (H/M/L) yazılır. LSB (en düşük bayt) yazıldığı anda frekans "latch" olur, yani aktifleşir. Kod tam sayı matematiği kullanır (float yok), o yüzden hafif ve hatasızdır.

> **Sınır:** SX1255 sadece **400–510 MHz** arası çalışır. Bu aralığın dışında bir değer girersen `UYARI: 400-510 MHz disinda!` der ama yine de yazar (kilitlenmeyebilir).

### TX Gücü

| Komut | Açıklama |
|-------|----------|
| `txpower <0-15>` | Tek knob TX gücü (0=min, 15=max) — **kolay yol** |
| `txgain <dac0-3> <mix0-15>` | Tam kontrol (baseband + mixer ayrı) |

**Örnek — tek knob:**
```
> txpower 15
  TXFE1=0x3F
  TX gain -> dac=3 (0 dB), mix=15 (~-7 dB bagil)
  yaklasik cikis: ~7 dBm (kaba, olcerek dogrula)

> txpower 6
  TXFE1=0x16
  TX gain -> dac=1 (-6 dB), mix=6 (~-25 dB bagil)
  yaklasik cikis: ~-31 dBm (kaba, olcerek dogrula)
```

**Örnek — tam kontrol:**
```
> txgain 3 10
  TXFE1=0x3A
  TX gain -> dac=3 (0 dB), mix=10 (~-17 dB bagil)
```

**Güç zinciri (datasheet):**
- **Mixer gain** (`mix`, 0-15): ana güç knob'u, ~2 dB adım, toplam ~30 dB aralık. Formül: `≈ -37.5 + 2×mix` dB (bağıl).
- **DAC gain** (`dac`, 0-3): baseband seviyesi, 3 dB adım (`-9/-6/-3/0` dB).

> ⚠️ **dBm değerleri KABADIR.** Gerçek çıkış gücü balun'a, SMA eşlemesine, antene ve kalibrasyona bağlıdır. Bu sayılar **bağıl** göstergedir ("15 tam, 8 orta, 0 kısık"). Mutlak dBm için TX SMA'ya güç metresi/osiloskop koyup ölç.

> ⚠️ **SX1255 güçlü bir PA değildir.** Max çıkış ~**+7 dBm** (birkaç mW). Gerçek menzil/güç için harici RF güç yükselteci gerekir.

### Ham Register Erişimi (ileri seviye)

| Komut | Açıklama |
|-------|----------|
| `reg <addr> <val>` | Herhangi bir register'a ham yaz (hex) |
| `rd <addr>` | Herhangi bir register'ı ham oku (hex) |

**Örnekler:**
```
> rd 0x11
  [0x11]=0x06
> reg 0x0A 0x60
  [0x0A]=0x60 geri=0x60
```
Bu iki komut, konsolda özel komutu olmayan her ayara erişmeni sağlar (PLL bandwidth, filtre bandwidth, loopback modları vb.). Datasheet'in register tablosuyla birlikte kullan.

### Diğer

| Komut | Açıklama |
|-------|----------|
| `rxgain <lna1-6> <pga0-15>` | RX kazancı (LNA + PGA) |
| `reset` | Manuel reset + yeniden bring-up |

---

## 4. Tipik Kullanım Senaryoları

### Senaryo A: RX'te dinleme (varsayılan)
Açılışta zaten RX 433 MHz'de aktif. Sadece frekansı değiştir:
```
> freq rx 433.92
> rxgain 1 15          (max hassasiyet)
> stat                 (PLLrx=1 mi bak)
```

### Senaryo B: TX ile yayın (dikkatli)
```
> rx off               (RX'i kapat - istersen açık da bırakabilirsin)
> freq tx 434.0        (TX frekansini ayarla)
> tx on                (TX zincirini ac, PLL kilitlensin)
> txpower 8            (once gucu ayarla - dusuk baslat!)
> pa on                (SON adim: PA'yi ac, RF cikar)
...
> pa off               (yayini bitir)
> tx off
```

### Senaryo C: Enerji tasarrufu
```
> standby              (kristal acik, radyo kapali - hizli uyanir)
> sleep                (her sey kapali - en dusuk guc)
```

---

## 5. LED Göstergesi

Kart üzerindeki LED1, `stat` okumasına bakmadan durumu anlamanı sağlar:

| LED durumu | Anlamı |
|------------|--------|
| Hızlı yanıp sönme (~100 ms) | Sistem sağlıklı (XOSC + ilgili PLL kilitli) |
| Yavaş yanıp sönme (~500 ms) | Bir sorun var (XOSC yok ya da PLL kilitlenmemiş) |

Ayrıca SX1255'in **DIO0** bacağı (kartta LED'i varsa) donanımsal olarak RX PLL kilidini gösterir — yazılımdan bağımsız bir teyit.

---

## 6. Kod İçi Önemli Fonksiyonlar (Geliştirici Notları)

| Fonksiyon | Görevi |
|-----------|--------|
| `sx_read/write(addr,val)` | Tek register SPI erişimi |
| `sx_hz_to_frf(hz)` | Hz → 24-bit frekans register değeri |
| `sx_set_freq(is_tx,hz)` | FRF register'larını yaz (LSB latch eder) |
| `sx_apply_mode()` | `mode_shadow`'u yaz + PLL kilidini bekle/raporla |
| `sx_bringup()` | Açılış dizisi: XOSC + config + RX aç |
| `wait_stat(mask,ms)` | Bir STAT bitini timeout'lu bekle |
| `process_line(s)` | Komut satırını ayrıştır ve çalıştır |
| `HAL_UARTEx_RxEventCallback` | UART kesmesi: baytları topla, satır kur |

### Komut nasıl eklenir?
`process_line()` içindeki `else if (str_eq(argv[0], "..."))` zincirine yeni bir dal ekle. Örnek: TX filtre bandwidth ayarı için:
```c
else if (str_eq(argv[0], "txbw") && argc >= 2) {
    uint8_t bw = (uint8_t)strtoul(argv[1], NULL, 0) & 0x1F;
    uint8_t cur = sx_read(0x0A);           // TXFE3
    sx_write(0x0A, (cur & 0xE0) | bw);     // alt 5 bit = tx_filter_bw
    uart_str("  txbw ayarlandi\r\n");
}
```

---

## 7. Uyarılar ve Sık Karşılaşılan Sorunlar

### ⚠️ Donanım / Güvenlik

- **PA'yı antensiz sürme.** `pa on` demeden önce TX SMA'ya uygun anten veya 50 Ω dummy load bağlı olsun. Açık/kısa devre yükte yansıyan güç PA'yı zorlar. Datasheet optimum yükü 100 Ω differential belirtir (kartta balun bunu çevirir).
- **Güç ayarını `pa on`'dan ÖNCE yap.** Sıra: `tx on` → `txpower N` → `pa on`. Böylece PA'yı beklenmedik yüksek güçle başlatmazsın.
- **Yayın düzenlemeleri.** 433 MHz ISM bandında bile yayın gücü, görev döngüsü ve anten için yerel telsiz kurallarına uy. Antenle havaya basıyorsan bu senin sorumluluğunda.
- **dBm değerleri kabadır** (§3 TX Gücü). Kritik ölçümler için güç metresi kullan.

### ⚠️ Yazılım / Konfigürasyon

- **USART2 kesmesi açık olmalı.** Firmware `HAL_UARTEx_ReceiveToIdle_IT` kullanır. CubeMX → USART2 → **NVIC Settings → "USART2 global interrupt" ✔**. Kapalıysa hiçbir komut algılanmaz (kart sadece açılış mesajını basar, sonra sessiz kalır).
- **Satır sonu karakteri.** Terminalin **CR** (`\r`) veya **LF** (`\n`) göndermeli. Çoğu terminalde default vardır; komutlar çalışmıyorsa terminal "Enter = CR/LF" ayarını kontrol et.
- **Baud/format:** kesinlikle **115200 8N1**. Yanlış baud → ekranda çöp karakterler.
- **CS pini SPI_NSS_SOFT olmalı.** Hardware NSS + pulse modu SX1255 frame'ini bozar (bu proje boyunca yaşandı). CubeMX'te SPI1 NSS = Disable, CS ayrı GPIO.

### ⚠️ Davranışsal

- `tx off` verince **PA otomatik kapanır** (güvenlik için `driver_enable` de temizlenir). Tekrar açmak için `tx on` sonra `pa on`.
- `sleep` verince XOSC de kapanır; tekrar kullanmak için `reset` ya da `standby`/`rx on` ile uyandır (XOSC'un yeniden hazır olması ~ms alır).
- Frekans değiştirince PLL'in yeniden kilitlenmesi birkaç mikrosaniye-milisaniye sürer; `stat` ile teyit et.

### Sorun Giderme Tablosu

| Belirti | Olası neden | Çözüm |
|---------|-------------|-------|
| Açılış mesajı gelir, komut çalışmaz | USART2 kesmesi kapalı | CubeMX NVIC'te aç |
| Ekranda çöp karakter | Yanlış baud | 115200 8N1 yap |
| `XOSC=0`, LED yavaş | Kristal/lehim sorunu | XTA-XTB kısa devre/lehim kontrol |
| `PLLtx=0` ama tx açık | TX PLL kilitlenmedi | Frekans 400-510 içinde mi, tekrar dene |
| Yazdığım harfleri görmüyorum | Local echo kapalı | Terminalde local echo aç |

---

## 8. Özet Komut Kartı

```
DURUM      help | stat | mon on|off
GUC/MOD    rx on|off | tx on|off | pa on|off | standby | sleep
FREKANS    freq rx|tx|both <MHz>
TX GUCU    txpower <0-15>  |  txgain <dac0-3> <mix0-15>
RX KAZANC  rxgain <lna1-6> <pga0-15>
HAM        reg <addr> <val> | rd <addr>       (hex)
SISTEM     reset
```

**Güvenli TX dizisi:** `tx on` → `txpower N` → `pa on` → ... → `pa off` → `tx off`

---

*Bu firmware ve rehber, Dand-IQ PMOD kartındaki SX1255'in bring-up ve kontrol katmanıdır. Ham IQ akışı (I_OUT/Q_OUT/CLK_OUT) FPGA tarafında yakalanır — o katman bu belgenin kapsamı dışındadır.*