"""
Dand-IQ / SX1255 Mode A analiz araci.

KULLANIM
--------
1. LogicAnalyzer GUI'de yakalama yap (CLK + I_OUT + Q_OUT)
2. File -> Export ile CSV olarak kaydet
3. python analyze.py            <- klasordeki en yeni CSV'yi alir
   python analyze.py dosya.csv  <- belirli bir dosya

HICBIR AYAR GEREKMIYOR.
Hangi kanalin CLK oldugunu, ornekleme hizini, kanal sirasini
kendisi bulur.

Gereken:  pip install numpy pandas matplotlib
"""

import os
import sys
import glob
import numpy as np
import matplotlib.pyplot as plt

CLK_FREQ  = 36e6     # SX1255 CLK_OUT
DECIMATION = 64      # 36 MHz / 64 = 562.5 kSps


# ==================================================================
# CSV yukleme - format ne olursa olsun
# ==================================================================
def load_any_csv(path):
    """Ayirici, baslik, sutun sayisi ne olursa olsun okumaya calisir."""
    with open(path, "r", errors="ignore") as f:
        head = [f.readline() for _ in range(5)]

    # Ayiriciyi bul
    sep = max([",", ";", "\t", " "],
              key=lambda s: sum(line.count(s) for line in head))

    # Yorum/baslik satirlarini atla
    skip = 0
    for line in head:
        s = line.strip()
        if not s or s.startswith((";", "#", "//")):
            skip += 1
            continue
        toks = [t.strip() for t in s.split(sep) if t.strip()]
        if toks and all(t.lstrip("-").replace(".", "", 1).isdigit()
                        for t in toks):
            break
        skip += 1

    d = np.genfromtxt(path, delimiter=sep, skip_header=skip,
                      invalid_raise=False)

    if d.ndim == 1:
        d = d.reshape(-1, 1)

    # Tamamen NaN olan sutunlari at
    good = ~np.all(np.isnan(d), axis=0)
    d = d[:, good]
    d = d[~np.any(np.isnan(d), axis=1)]

    if d.shape[0] < 100:
        raise SystemExit(
            f"HATA: '{path}' icinde sadece {d.shape[0]} kullanilabilir satir var."
        )

    # 0/1'e cevir
    return (d > 0.5).astype(np.int8)


# ==================================================================
# Hangi kanal CLK? -> en duzenli olan
# ==================================================================
def find_clock(d):
    """
    Saat kanali: gecis sayisi cok VE gecis araliklari cok duzenli.
    I/Q ise sigma-delta oldugu icin duzensiz.
    """
    best, best_score = None, -1
    report = []

    for c in range(d.shape[1]):
        ch = d[:, c]
        edges = np.flatnonzero(np.diff(ch) > 0) + 1
        if len(edges) < 20:
            report.append((c, len(edges), float("inf"), 0.0))
            continue

        sp = np.diff(edges)
        med = np.median(sp)
        jitter = np.std(sp) / med if med > 0 else 999
        duty = ch.mean()

        # Duzenlilik skoru: jitter dusuk + duty %50'ye yakin
        score = (1.0 / (1.0 + jitter * 10)) * (1.0 - abs(duty - 0.5) * 2)
        report.append((c, len(edges), jitter, duty))

        if score > best_score:
            best_score, best = score, c

    return best, report


# ==================================================================
# Ana akis
# ==================================================================
def main():
    # --- dosyayi bul ---
    if len(sys.argv) > 1:
        path = sys.argv[1]
        if not os.path.isfile(path):
            raise SystemExit(f"HATA: '{path}' bulunamadi.")
    else:
        csvs = [f for f in glob.glob("*.csv")]
        if not csvs:
            raise SystemExit(
                "Klasorde CSV yok.\n\n"
                "  1. LogicAnalyzer GUI'de yakalama yap\n"
                "  2. File -> Export ile CSV kaydet (bu klasore)\n"
                "  3. Tekrar calistir"
            )
        path = max(csvs, key=os.path.getmtime)

    print("=" * 62)
    print(f"  Dosya: {path}")
    print("=" * 62)

    d = load_any_csv(path)
    print(f"  {d.shape[0]} ornek, {d.shape[1]} kanal")

    if d.shape[1] < 3:
        raise SystemExit(
            f"HATA: 3 kanal gerekiyor (CLK, I, Q), {d.shape[1]} bulundu."
        )

    # --- CLK'yi bul ---
    clk_idx, report = find_clock(d)

    print()
    print("  kanal   gecis   jitter   duty")
    for c, n, j, duty in report:
        mark = "  <- CLK" if c == clk_idx else ""
        js = "  -  " if j == float("inf") else f"{j:5.3f}"
        print(f"    {c}   {n:6d}   {js}   {duty:.3f}{mark}")

    if clk_idx is None:
        raise SystemExit(
            "\nHATA: Saat kanali bulunamadi.\n"
            "  Hicbir kanalda duzenli gecis yok. CLK bagli mi?"
        )

    others = [c for c in range(d.shape[1]) if c != clk_idx][:2]
    clk = d[:, clk_idx]
    i_ch = d[:, others[0]]
    q_ch = d[:, others[1]]

    if i_ch.std() == 0 or q_ch.std() == 0:
        print()
        print("  !! I veya Q hic degismiyor.")
        print("     Konsolda 'rx on' yazdin mi?")

    # --- ornekleme hizini CLK'den cikar ---
    edges = np.flatnonzero(np.diff(clk) > 0) + 1
    spacing = float(np.median(np.diff(edges)))
    fs = CLK_FREQ * spacing

    print()
    print(f"  CLK kenari      : {len(edges)}")
    print(f"  kenar araligi   : {spacing:.2f} ornek")
    print(f"  -> ornekleme    : ~{fs/1e6:.0f} MSa/s  (yaklasik; CLK 36 MHz varsayimiyla)")

    if spacing < 2.5:
        print("  !! Bit basina 2.5 ornekten az - kenar tespiti guvenilmez.")
        print("     GUI'de daha yuksek ornekleme hizi kullan.")

    # --- bit ortasinda ornekle ---
    offset = max(1, int(round(spacing / 2)))
    idx = edges[:-1] + offset
    idx = idx[idx < len(i_ch)]

    bits = (i_ch[idx].astype(float) * 2 - 1) \
         + 1j * (q_ch[idx].astype(float) * 2 - 1)

    print(f"  cozulen bit     : {len(bits)}")
    print(f"  sure            : {len(bits)/CLK_FREQ*1e6:.1f} us")

    if len(bits) < DECIMATION * 8:
        raise SystemExit(
            f"\nHATA: Cok az bit ({len(bits)}). Daha uzun yakalama yap."
        )

    # --- decimate ---
    n = len(bits) // DECIMATION * DECIMATION
    dec = bits[:n].reshape(-1, DECIMATION).mean(axis=1)
    fs_dec = CLK_FREQ / DECIMATION

    dc = complex(np.mean(dec))
    x = dec - dc

    # --- spektrum ---
    w = np.blackman(len(x))
    X = np.fft.fftshift(np.fft.fft(x * w))
    mag = 20 * np.log10(np.abs(X) / (len(x) * 0.42) + 1e-12)
    freq = np.fft.fftshift(np.fft.fftfreq(len(x), 1 / fs_dec))

    peak = int(np.argmax(mag))
    noise = np.median(mag)

    print()
    print(f"  cikis hizi      : {fs_dec/1e3:.1f} kSps")
    print(f"  FFT cozunurluk  : {fs_dec/len(x)/1e3:.2f} kHz")
    print(f"  DC offset       : I={dc.real:+.4f}  Q={dc.imag:+.4f}")
    print(f"  en guclu tepe   : {freq[peak]/1e3:+.1f} kHz   {mag[peak]:.1f} dB")
    print(f"  gurultu tabani  : {noise:.1f} dB")
    print(f"  tepe/gurultu    : {mag[peak]-noise:.1f} dB")
    print()

    if mag[peak] - noise > 12:
        print("  >> SINYAL VAR. Tepe gurultunun belirgin sekilde ustunde.")
    else:
        print("  >> Belirgin bir tepe yok. Sadece gurultu goruluyor.")
        print("     TinySA/Pluto'dan RX LO'dan ~100 kHz kaydirilmis ton bas.")
    print()

    # --- grafikler ---
    fig, ax = plt.subplots(3, 1, figsize=(11, 9))

    m = min(200, len(clk))
    ax[0].step(range(m), clk[:m] * 0.8 + 2.2, where="post", label=f"CLK (kanal {clk_idx})")
    ax[0].step(range(m), i_ch[:m] * 0.8 + 1.1, where="post", label=f"I (kanal {others[0]})")
    ax[0].step(range(m), q_ch[:m] * 0.8 + 0.0, where="post", label=f"Q (kanal {others[1]})")
    ax[0].set_title("Ham dijital (ilk 200 ornek)")
    ax[0].set_yticks([])
    ax[0].legend(loc="upper right", fontsize=8)
    ax[0].grid(alpha=0.3)

    k = min(400, len(dec))
    ax[1].plot(np.real(dec[:k]), lw=1, label="I")
    ax[1].plot(np.imag(dec[:k]), lw=1, label="Q")
    ax[1].set_title("Decimate edilmis I/Q  (ton varsa duzgun sinus cifti)")
    ax[1].set_xlabel(f"ornek @ {fs_dec/1e3:.1f} kSps")
    ax[1].legend(loc="upper right", fontsize=8)
    ax[1].grid(alpha=0.3)

    ax[2].plot(freq / 1e3, mag, lw=1)
    ax[2].axvline(freq[peak] / 1e3, color="r", ls="--", alpha=0.6)
    ax[2].axhline(noise, color="gray", ls=":", alpha=0.6)
    ax[2].set_title(f"Spektrum   -   tepe {freq[peak]/1e3:+.1f} kHz, "
                    f"{mag[peak]-noise:.1f} dB gurultu ustu")
    ax[2].set_xlabel("kHz (RX LO'ya gore)")
    ax[2].set_ylabel("dB")
    ax[2].grid(alpha=0.3)

    plt.tight_layout()
    out = os.path.splitext(path)[0] + "_analiz.png"
    plt.savefig(out, dpi=110)
    print(f"  Grafik kaydedildi: {out}")
    plt.show()


if __name__ == "__main__":
    main()