# Kod Rehberi — SX1255 Kontrol Konsolu (`main.c` v6.1)

Bu belge firmware'in **kod tarafını** anlatır: dosya yapısı, gerekli çevre birimi (peripheral) ayarları, fonksiyon-fonksiyon görevleri, bir komutun baştan sona akışı ve nasıl yeni komut ekleyeceğin. Radyo komutlarının *kullanımı* için → [`KULLANIM.md`](KULLANIM.md).

---

## 1. Derle & yükle

1. STM32CubeMX / CubeIDE'de bir STM32G0 projesi aç.
2. Aşağıdaki çevre birimlerini yapılandır (bkz. §2).
3. `firmware/main.c` içeriğini üretilen `main.c`'nin ilgili `USER CODE` bloklarına yerleştir (dosya zaten bu bloklarla yazılmıştır — direkt yapıştırabilirsin).
4. Derle → karta yükle (SWD).
5. Terminal aç: **115200 8N1**.

> Kod yalnızca `main.h`, `<string.h>`, `<stdlib.h>` ve STM32 HAL kullanır. Ek kütüphane yok, `printf`/float yok — hafif ve deterministik.

---

## 2. Gerekli peripheral yapılandırması

Bu ayarlar **doğru olmadan** kod çalışmaz. CubeMX'te birebir ayarla:

### SPI1 (SX1255 ile)
| Ayar | Değer | Neden |
|------|-------|-------|
| Mode | Master | MCU çipi sürer |
| Data size | 8 bit | Register erişimi bayt bazlı |
| CPOL / CPHA | Low / 1Edge (Mode 0) | SX1255 SPI zamanlaması |
| **NSS** | **Disable (Software)** | ⚠️ Hardware NSS + pulse SX1255 frame'ini bozar |
| First bit | MSB | — |
| Baud prescaler | ÷2 (uygun hız) | — |

`CS_SIGNAL` ayrı bir **GPIO Output** olarak elle sürülür (`SX_CS_LOW/HIGH`).

### USART2 (konsol)
| Ayar | Değer |
|------|-------|
| Baud | **115200** |
| Word / Stop / Parity | 8 / 1 / None (**8N1**) |
| Mode | TX + RX |
| **NVIC** | **USART2 global interrupt = ✔** |

> ⚠️ USART2 kesmesi kapalıysa `HAL_UARTEx_ReceiveToIdle_IT` çalışmaz → kart açılış mesajını basar ama **hiçbir komutu algılamaz**.

### GPIO (Output Push-Pull)
| Pin sembolü | Görev |
|-------------|-------|
| `CS_SIGNAL` | SX1255 chip select (yazılım CS) |
| `SX_RESET` | SX1255 donanım reset |
| `LED1` | Sağlık heartbeat LED'i |

### Clock
Kod HSI ile çalışır (`SystemClock_Config`), harici MCU kristali gerekmez. 36 MHz kristal **SX1255'e** aittir, MCU'ya değil.

---

## 3. Kod haritası (katmanlar)

```
process_line()  ← Konsol: metni komuta çevirir
      │
      ▼
sx_set_freq / sx_apply_mode / sx_bringup  ← Radyo kontrol: anlamlı işlevler
      │
      ▼
sx_read() / sx_write()  ← Sürücü: SPI 2-bayt register erişimi
```

---

## 4. Fonksiyon referansı

### Donanım sürücü
| Fonksiyon | Görevi |
|-----------|--------|
| `SX_CS_LOW/HIGH()` | CS pinini elle sür (SPI frame'i çerçevele) |
| `sx_read(addr)` | Register oku — `addr & 0x7F` gönder, 2. baytı al |
| `sx_write(addr,val)` | Register yaz — `addr \| 0x80` (write flag) + veri |
| `sx_manual_reset()` | RESET pinini darbele (1 ms high, 6 ms bekle) |

### Radyo kontrol
| Fonksiyon | Görevi |
|-----------|--------|
| `sx_hz_to_frf(hz)` | `Frf = hz × 2²⁰ / 36 MHz` — 64-bit tamsayı, float yok |
| `sx_set_freq(is_tx,hz)` | FRF H/M/L register'larını yaz; **LSB yazılınca frekans latch olur**. 400–510 MHz dışında uyarır |
| `sx_apply_mode()` | `mode_shadow`'u MODE reg'e yaz, ardından açık olan zincirin **PLL kilidini bekle & raporla** |
| `wait_stat(mask,ms)` | STAT register'ındaki bir biti timeout'lu bekle (PLL/XOSC lock) |
| `sx_bringup()` | Açılış dizisi: VERSION oku → XOSC aç → CK_SEL/IISM/RXFE config → 433 MHz set → RX aç. Başarı=1 |

### Konsol & G/Ç
| Fonksiyon | Görevi |
|-----------|--------|
| `process_line(s)` | Satırı token'lara böl, `argv[]` kur, komut zincirini çalıştır |
| `print_help/stat()` | Menü / durum çıktısı |
| `print_txgain(dac,mix)` | Yaklaşık (bağıl) TX seviyelerini raporla |
| `uart_str/hex/u32/i32()` | Küçük, ayrılıksız (dependency-free) yazıcılar |
| `HAL_UARTEx_RxEventCallback` | UART IDLE kesmesi: baytları `acc[]`'ye topla, `\r`/`\n` görünce `line_ready=1` |

---

## 5. Bir komutun yaşam döngüsü

```
Kullanıcı "freq rx 433.5\r" yazar
        │
        ▼
UART IDLE kesmesi → RxEventCallback: baytlar acc[]'ye, satır tamamlanınca line_ready=1
        │
        ▼
main() döngüsü: line_ready görür → process_line(line)
        │
        ▼
strtok ile argv = ["freq","rx","433.5"]
        │
        ▼
"freq" dalı → parse_mhz_to_hz("433.5") = 433500000
        │
        ▼
sx_set_freq(0, 433500000) → sx_hz_to_frf → 3× sx_write (H,M,L)  [LSB latch]
        │
        ▼
uart_str: "  RX freq = 433500000 Hz (Frf=...)"  →  "> "
```

**Önemli:** UART alımı kesme + IDLE ile yapılır (`ReceiveToIdle_IT`); komut *işleme* ana döngüde olur. Kesme içinde ağır iş yapılmaz — sadece bayt toplanır. Bu, SPI/HAL çağrılarının kesme bağlamında bloklamasını önler.

---

## 6. Kullanılan register haritası (`#define`'lar)

| Sembol | Adres | İçerik |
|--------|:-----:|--------|
| `SX_MODE` | 0x00 | ref/rx/tx/driver enable bitleri |
| `SX_FRFH/M/L_RX` | 0x01–0x03 | RX frekans (24-bit) |
| `SX_FRFH/M/L_TX` | 0x04–0x06 | TX frekans (24-bit) |
| `SX_VERSION` | 0x07 | Çip versiyonu (bring-up doğrulaması) |
| `SX_TXFE1` | 0x08 | `[6:4]` DAC gain · `[3:0]` mixer gain |
| `SX_RXFE1/2` | 0x0C/0x0D | RX LNA/PGA kazancı |
| `SX_CK_SEL` | 0x10 | Saat seçimi |
| `SX_STAT` | 0x11 | XOSC(0x04)·PLLrx(0x02)·PLLtx(0x01) lock bitleri |
| `SX_IISM` | 0x12 | IQ arayüz modu |

MODE bit maskeleri: `M_REF=0x01`, `M_RX=0x02`, `M_TX=0x04`, `M_DRV=0x08`.

---

## 7. Yeni komut ekleme

`process_line()` içindeki `else if` zincirine bir dal ekle. Örnek — TX filtre bandwidth ayarı:

```c
else if (str_eq(argv[0], "txbw") && argc >= 2) {
    uint8_t bw  = (uint8_t)strtoul(argv[1], NULL, 0) & 0x1F;
    uint8_t cur = sx_read(0x0A);              // TXFE3
    sx_write(0x0A, (cur & 0xE0) | bw);        // alt 5 bit = tx_filter_bw
    uart_str("  txbw ayarlandi\r\n");
}
```

Kurallar:
- `argc` ile argüman sayısını **kontrol et** (eksikse çökme).
- Sayı okurken `strtoul(x, NULL, 0)` → `0x..` hex'i otomatik anlar.
- Register'a yazmadan önce ilgili bitleri **oku-değiştir-yaz** ile koru.
- İşlem sonunda kullanıcıya kısa bir onay bas.

---

## 8. Kod tarafı tuzakları

- **SPI NSS mutlaka software.** Hardware NSS/pulse SX1255 2-bayt frame'ini böler (bu projede yaşandı).
- **`tx off` → PA otomatik kapanır** (`M_DRV` de temizlenir) — güvenlik için bilinçli.
- **`sleep` XOSC'u da kapatır**; uyandırmak için `reset` veya `standby`/`rx on` (XOSC ~ms sürede hazırlanır).
- Frekans değişiminde PLL yeniden kilitlenir (µs–ms) → `stat` ile teyit et.
- `acc[]`/`line[]` tamponu `RXBUF=96` bayt; daha uzun satır sessizce kırpılır.

---

*Kod: `firmware/main.c` · Firmware v6.1 · 433 MHz Mode A · 36 MHz XOSC · USART2 115200 8N1*
