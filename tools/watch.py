#!/usr/bin/env python3
"""
Klasoru izler, yeni CSV gelince otomatik analiz eder ve grafigi yeniler.

KULLANIM
--------
1. Bu scripti calistir:  python watch.py
2. LogicAnalyzer GUI'de yakala -> File -> Export -> bu klasore kaydet
3. Grafik kendiliginden guncellenir

Ctrl+C ile cik.
Ayni klasorde analyze.py olmali.
"""

import os
import glob
import time
import importlib.util

import numpy as np
import matplotlib.pyplot as plt

# analyze.py'yi modul olarak yukle
spec = importlib.util.spec_from_file_location("az", "analyze.py")
az = importlib.util.module_from_spec(spec)
spec.loader.exec_module(az)

CLK_FREQ = az.CLK_FREQ
D = az.DECIMATION


def process(path):
    d = az.load_any_csv(path)
    if d.shape[1] < 3:
        raise ValueError(f"3 kanal gerekiyor, {d.shape[1]} var")

    clk_idx, _ = az.find_clock(d)
    if clk_idx is None:
        raise ValueError("saat kanali bulunamadi")

    others = [c for c in range(d.shape[1]) if c != clk_idx][:2]
    clk, i_ch, q_ch = d[:, clk_idx], d[:, others[0]], d[:, others[1]]

    edges = np.flatnonzero(np.diff(clk) > 0) + 1
    spacing = float(np.median(np.diff(edges)))
    offset = max(1, int(round(spacing / 2)))
    idx = edges[:-1] + offset
    idx = idx[idx < len(i_ch)]

    bits = (i_ch[idx].astype(float) * 2 - 1) + 1j * (q_ch[idx].astype(float) * 2 - 1)
    n = len(bits) // D * D
    if n == 0:
        raise ValueError("cok az bit")
    dec = bits[:n].reshape(-1, D).mean(axis=1)

    fs_dec = CLK_FREQ / D
    dc = complex(np.mean(dec))
    x = dec - dc
    w = np.blackman(len(x))
    X = np.fft.fftshift(np.fft.fft(x * w))
    mag = 20 * np.log10(np.abs(X) / (len(x) * 0.42) + 1e-12)
    freq = np.fft.fftshift(np.fft.fftfreq(len(x), 1 / fs_dec))

    return dec, freq, mag, fs_dec, dc, clk_idx


def main():
    plt.ion()
    fig, (ax_t, ax_f) = plt.subplots(2, 1, figsize=(11, 7))

    ln_i, = ax_t.plot([], [], lw=1, label="I")
    ln_q, = ax_t.plot([], [], lw=1, label="Q")
    ax_t.legend(loc="upper right", fontsize=8)
    ax_t.grid(alpha=0.3)

    ln_s, = ax_f.plot([], [], lw=1)
    vline = ax_f.axvline(0, color="r", ls="--", alpha=0.6)
    ax_f.set_xlabel("kHz (RX LO'ya gore)")
    ax_f.set_ylabel("dB")
    ax_f.grid(alpha=0.3)

    print("Klasor izleniyor. GUI'den CSV export et, grafik yenilenir.")
    print("Ctrl+C ile cik.\n")

    last = None
    while True:
        csvs = glob.glob("*.csv")
        if csvs:
            newest = max(csvs, key=os.path.getmtime)
            stamp = (newest, os.path.getmtime(newest))

            if stamp != last:
                # dosya yazimi bitsin
                time.sleep(0.4)
                try:
                    dec, freq, mag, fs_dec, dc, clk_idx = process(newest)
                except Exception as e:
                    print(f"[{newest}] okunamadi: {e}")
                    last = stamp
                    time.sleep(0.5)
                    continue

                last = stamp

                k = min(400, len(dec))
                ln_i.set_data(np.arange(k), np.real(dec[:k]))
                ln_q.set_data(np.arange(k), np.imag(dec[:k]))
                ax_t.set_xlim(0, k)
                lim = max(0.05, np.abs(dec[:k]).max() * 1.2)
                ax_t.set_ylim(-lim, lim)
                ax_t.set_title(f"{newest}   |   I/Q @ {fs_dec/1e3:.1f} kSps"
                               f"   (CLK = kanal {clk_idx})")

                ln_s.set_data(freq / 1e3, mag)
                ax_f.set_xlim(freq[0] / 1e3, freq[-1] / 1e3)
                ax_f.set_ylim(mag.min() - 5, mag.max() + 8)

                peak = int(np.argmax(mag))
                noise = float(np.median(mag))
                snr = mag[peak] - noise
                vline.set_xdata([freq[peak] / 1e3] * 2)

                verdict = "SINYAL VAR" if snr > 12 else "sadece gurultu"
                ax_f.set_title(f"tepe {freq[peak]/1e3:+.1f} kHz   "
                               f"{snr:.1f} dB gurultu ustu   ->  {verdict}")

                print(f"[{time.strftime('%H:%M:%S')}] {newest:28s} "
                      f"tepe {freq[peak]/1e3:+8.1f} kHz   "
                      f"SNR {snr:5.1f} dB   "
                      f"DC I={dc.real:+.3f} Q={dc.imag:+.3f}   {verdict}")

        plt.pause(0.3)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nCikildi.")
