#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, scrolledtext
import threading
import socket
import serial
import time
import datetime
import queue
from itertools import permutations

# ----------------------------------------------------------------------
# Sistem Parametreleri
# ----------------------------------------------------------------------
SYNC = bytes([0x65, 0x65])
HDR_LEN = 3
PAYLOAD_LEN = 32
MSG_LEN = 12  # Sadece "GRGA" + 8 haneli sira numarasi (12 bayt)
SYNC_BITS = ''.join(f'{b:08b}' for b in SYNC)


# ----------------------------------------------------------------------
# Çekirdek Çözücü Algoritmaları
# ----------------------------------------------------------------------
def symbols_to_bits(symbols, perm):
    out = []
    for s in symbols:
        v = perm[s & 3]
        out.append('1' if (v >> 1) & 1 else '0')
        out.append('1' if v & 1 else '0')
    return ''.join(out)


def bits_to_bytes(bits, offset, count):
    out = bytearray()
    for i in range(count):
        chunk = bits[offset + i * 8: offset + i * 8 + 8]
        if len(chunk) < 8: break
        out.append(int(chunk, 2))
    return bytes(out)


class PermFinder:
    def __init__(self, frame_len):
        self.frame_len = frame_len
        self.frame_bits = frame_len * 8
        self.perms = list(permutations([0, 1, 2, 3]))

    def score(self, symbols, perm):
        bits = symbols_to_bits(symbols, perm)
        hits = []
        pos = bits.find(SYNC_BITS)
        while pos != -1 and len(hits) < 64:
            hits.append(pos)
            pos = bits.find(SYNC_BITS, pos + 1)
        if len(hits) < 3: return 0, None
        best_cnt, best_off = 0, None
        for h in hits[:8]:
            cnt = sum(1 for x in hits if (x - h) % self.frame_bits == 0)
            if cnt > best_cnt:
                best_cnt, best_off = cnt, h % self.frame_bits
        return best_cnt, best_off

    def find(self, symbols):
        best = (0, None, None)
        for p in self.perms:
            cnt, off = self.score(symbols, p)
            if cnt > best[0]: best = (cnt, p, off)
        return best


class FrameDecoder:
    def __init__(self):
        self.frame_len = HDR_LEN + PAYLOAD_LEN
        self.finder = PermFinder(self.frame_len)
        self.perm = None
        self.offset = None
        self.locked = False
        self.bit_buffer = ""
        self.n_sync_err = 0

    def try_lock(self, symbols):
        score, perm, off = self.finder.find(symbols)
        if score >= 3:
            self.perm, self.offset, self.locked = perm, off, True
            self.bit_buffer = ""
            return True, score
        return False, score

    def parse(self, symbols):
        bits = symbols_to_bits(symbols, self.perm)
        self.bit_buffer += bits
        if self.offset is not None:
            self.bit_buffer = self.bit_buffer[self.offset:]
            self.offset = None

        fb = self.frame_len * 8
        out = []
        while len(self.bit_buffer) >= fb:
            frame = bits_to_bytes(self.bit_buffer, 0, self.frame_len)
            self.bit_buffer = self.bit_buffer[fb:]
            if frame[0] != 0x65 or frame[1] != 0x65:
                self.n_sync_err += 1
                if self.n_sync_err > 20:
                    self.locked, self.bit_buffer = False, ""
                    break
                continue
            self.n_sync_err = 0
            n = frame[2]
            if n > 0 and n <= PAYLOAD_LEN:
                out.append(frame[HDR_LEN:HDR_LEN + n])
        return out


# ----------------------------------------------------------------------
# Arayüz ve Arka Plan İşlemleri
# ----------------------------------------------------------------------
class GargaHILApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Garga Labs - Dand-IQ HIL Testbench")
        self.root.geometry("950x550")
        self.root.configure(bg="#2E3440")

        # Paylaşılan Durum Değişkenleri
        self.running = False
        self.locked = False
        self.tx_count = 0
        self.rx_ok = 0
        self.rx_missed = 0
        self.overhead_pkts = 0
        self.log_queue = queue.Queue()

        self.setup_ui()
        self.root.after(100, self.process_logs)

    def setup_ui(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TLabel", background="#2E3440", foreground="#D8DEE9", font=("Segoe UI", 10))
        style.configure("StatValue.TLabel", font=("Consolas", 24, "bold"), foreground="#A3BE8C")
        style.configure("StatTitle.TLabel", font=("Segoe UI", 10), foreground="#4C566A")

        # Üst Kısım: Kontroller
        top_frame = tk.Frame(self.root, bg="#3B4252", pady=10, padx=10)
        top_frame.pack(fill=tk.X)

        ttk.Label(top_frame, text="COM Port:").pack(side=tk.LEFT, padx=5)
        self.port_entry = ttk.Entry(top_frame, width=8)
        self.port_entry.insert(0, "COM6")
        self.port_entry.pack(side=tk.LEFT, padx=5)

        ttk.Label(top_frame, text="Baudrate:").pack(side=tk.LEFT, padx=5)
        self.baud_entry = ttk.Entry(top_frame, width=8)
        self.baud_entry.insert(0, "125000")
        self.baud_entry.pack(side=tk.LEFT, padx=5)

        ttk.Label(top_frame, text="Hız (Pkt/sn):").pack(side=tk.LEFT, padx=5)
        self.speed_entry = ttk.Entry(top_frame, width=6)
        self.speed_entry.insert(0, "50")
        self.speed_entry.pack(side=tk.LEFT, padx=5)

        ttk.Label(top_frame, text="UDP Port:").pack(side=tk.LEFT, padx=5)
        self.udp_entry = ttk.Entry(top_frame, width=6)
        self.udp_entry.insert(0, "2345")
        self.udp_entry.pack(side=tk.LEFT, padx=5)

        self.btn_start = tk.Button(top_frame, text="TESTİ BAŞLAT", bg="#81A1C1", fg="white",
                                   font=("Segoe UI", 10, "bold"), command=self.toggle_test)
        self.btn_start.pack(side=tk.LEFT, padx=15)

        # Orta Kısım: Göstergeler (Dashboard)
        dash_frame = tk.Frame(self.root, bg="#2E3440", pady=20)
        dash_frame.pack(fill=tk.X)

        # Sol Taraf: Durum ve Maliyet
        status_frame = tk.Frame(dash_frame, bg="#2E3440")
        status_frame.grid(row=0, column=0, rowspan=2, padx=20)

        self.lbl_status = tk.Label(status_frame, text="BEKLENİYOR", bg="#4C566A", fg="white",
                                   font=("Segoe UI", 14, "bold"), width=15)
        self.lbl_status.pack(pady=2)

        self.lbl_overhead = tk.Label(status_frame, text="Kilit Maliyeti: -", bg="#2E3440", fg="#EBCB8B",
                                     font=("Segoe UI", 9, "bold"))
        self.lbl_overhead.pack()

        # Sağ Taraf: Sayaçlar
        ttk.Label(dash_frame, text="İşlenen TX", style="StatTitle.TLabel").grid(row=0, column=1, padx=20)
        self.lbl_tx = ttk.Label(dash_frame, text="0", style="StatValue.TLabel")
        self.lbl_tx.grid(row=1, column=1)

        ttk.Label(dash_frame, text="RX (Başarılı)", style="StatTitle.TLabel").grid(row=0, column=2, padx=20)
        self.lbl_rx = ttk.Label(dash_frame, text="0", style="StatValue.TLabel")
        self.lbl_rx.grid(row=1, column=2)

        ttk.Label(dash_frame, text="Kayıp", style="StatTitle.TLabel").grid(row=0, column=3, padx=20)
        self.lbl_missed = ttk.Label(dash_frame, text="0", style="StatValue.TLabel", foreground="#BF616A")
        self.lbl_missed.grid(row=1, column=3)

        ttk.Label(dash_frame, text="Güvenilirlik", style="StatTitle.TLabel").grid(row=0, column=4, padx=20)
        self.lbl_rel = ttk.Label(dash_frame, text="% 0.00", style="StatValue.TLabel", foreground="#EBCB8B")
        self.lbl_rel.grid(row=1, column=4)

        # Alt Kısım: Log Ekranı
        log_frame = tk.Frame(self.root, bg="#2E3440", padx=10, pady=10)
        log_frame.pack(fill=tk.BOTH, expand=True)

        self.text_log = scrolledtext.ScrolledText(log_frame, bg="#21252B", fg="#A9B7C6", font=("Consolas", 10),
                                                  state='disabled')
        self.text_log.pack(fill=tk.BOTH, expand=True)

    def write_log(self, msg):
        """Log kuyruğuna mesaj atar (Thread-safe)"""
        t_stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_queue.put(f"[{t_stamp}] {msg}\n")

    def process_logs(self):
        """Kuyruktaki logları alıp Tkinter Text widget'ına basar"""
        try:
            while True:
                msg = self.log_queue.get_nowait()
                self.text_log.configure(state='normal')
                self.text_log.insert(tk.END, msg)
                self.text_log.see(tk.END)
                self.text_log.configure(state='disabled')
        except queue.Empty:
            pass

        # Arayüz değerlerini güncelle (Sadece Kilit Sonrası İşlenen Paketler)
        processed_tx = self.rx_ok + self.rx_missed

        self.lbl_tx.config(text=f"{processed_tx}")
        self.lbl_rx.config(text=f"{self.rx_ok}")
        self.lbl_missed.config(text=f"{self.rx_missed}")

        rel = (self.rx_ok / processed_tx * 100) if processed_tx > 0 else 0.0
        self.lbl_rel.config(text=f"% {rel:.2f}")

        if self.running:
            if self.locked:
                self.lbl_status.config(text="KİLİTLENDİ", bg="#A3BE8C", fg="black")
                self.lbl_overhead.config(text=f"Kilit Maliyeti: {self.overhead_pkts} pkt")
            else:
                self.lbl_status.config(text="ARANIYOR...", bg="#BF616A", fg="white")
                self.lbl_overhead.config(text="Kilit Maliyeti: Hesaplanıyor")
        else:
            self.lbl_status.config(text="DURDURULDU", bg="#4C566A", fg="white")
            self.lbl_overhead.config(text="Kilit Maliyeti: -")

        self.root.after(100, self.process_logs)

    def toggle_test(self):
        if not self.running:
            self.running = True
            self.btn_start.config(text="TESTİ DURDUR", bg="#BF616A")
            self.tx_count = self.rx_ok = self.rx_missed = self.overhead_pkts = 0
            self.locked = False
            self.text_log.configure(state='normal')
            self.text_log.delete(1.0, tk.END)
            self.text_log.configure(state='disabled')

            threading.Thread(target=self.uart_tx_thread, daemon=True).start()
            threading.Thread(target=self.udp_rx_thread, daemon=True).start()
        else:
            self.running = False
            self.btn_start.config(text="TESTİ BAŞLAT", bg="#81A1C1")
            self.write_log("SYS: Test kullanıcı tarafından durduruldu.")

    def uart_tx_thread(self):
        port = self.port_entry.get().strip()

        try:
            baud = int(self.baud_entry.get().strip())
        except ValueError:
            self.write_log("HATA: Geçersiz Baudrate! 115200 kullanılıyor.")
            baud = 115200

        try:
            hiz_pkt = int(self.speed_entry.get().strip())
            bekleme_suresi = 1.0 / hiz_pkt
        except ValueError:
            self.write_log("HATA: Geçersiz Hız! Saniyede 50 paket kullanılıyor.")
            bekleme_suresi = 1.0 / 50.0

        try:
            ser = serial.Serial(port, baud, timeout=1)
            self.write_log(f"SYS: UART Açıldı ({port} @ {baud} bps | Hedef: {hiz_pkt} pkt/sn)")
            while self.running:
                msg = f"GRGA{self.tx_count:08d}"
                payload = msg.encode('utf-8')
                ser.write(payload)
                self.tx_count += 1
                time.sleep(bekleme_suresi)
            ser.close()
        except Exception as e:
            self.write_log(f"HATA: UART Port Hatası: {e}")
            self.running = False

    def udp_rx_thread(self):
        try:
            port = int(self.udp_entry.get().strip())
        except ValueError:
            self.write_log("HATA: Geçersiz UDP Portu! 2345 kullanılıyor.")
            port = 2345

        dec = FrameDecoder()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        sock.bind(('127.0.0.1', port))
        sock.settimeout(0.5)

        self.write_log(f"SYS: UDP Dinleniyor (127.0.0.1:{port})")

        buf = bytearray()
        rx_stream_buffer = bytearray()
        expected_seq = -1
        was_lock = False

        while self.running:
            try:
                pkt = sock.recv(65536)
                buf.extend(pkt)
            except socket.timeout:
                pass

            if not dec.locked and len(buf) >= 4000:
                ok, score = dec.try_lock(buf[:4000])
                if ok:
                    self.write_log(f"SYS: KİLİTLENDİ. Offset={dec.offset} bit")
                    self.locked = True
                    was_lock = True
                else:
                    buf = buf[-2000:]

            if dec.locked and len(buf) > 0:
                blocks = dec.parse(buf)
                buf.clear()

                for b in blocks:
                    rx_stream_buffer.extend(b)
                    while True:
                        idx = rx_stream_buffer.find(b'GRGA')
                        if idx == -1:
                            rx_stream_buffer = rx_stream_buffer[-3:]
                            break

                        # Düzeltilen kısım: Havuzdan 32 yerine gerçek mesaj uzunluğu (12 bayt) kesiliyor
                        if len(rx_stream_buffer) >= idx + MSG_LEN:
                            packet = rx_stream_buffer[idx: idx + MSG_LEN]
                            rx_stream_buffer = rx_stream_buffer[idx + MSG_LEN:]
                            try:
                                seq = int(packet[4:12].decode('utf-8'))

                                if expected_seq == -1:
                                    self.overhead_pkts = seq
                                    self.rx_ok = 1
                                    self.rx_missed = 0
                                    expected_seq = seq + 1
                                    self.write_log(
                                        f"SYS: Senkron oturdu. Kilitlenme Maliyeti: {self.overhead_pkts} Paket")
                                else:
                                    if seq != expected_seq:
                                        missed = seq - expected_seq
                                        if missed > 0:
                                            self.rx_missed += missed
                                            self.write_log(f"RX HATA: {missed} Paket Kayıp! (Beklenen: {expected_seq})")
                                    self.rx_ok += 1
                                    expected_seq = seq + 1

                                if seq % 50 == 0:
                                    self.write_log(f"RX OK: SEQ {seq:08d}")
                            except ValueError:
                                self.write_log("RX HATA: Bozuk Format")
                        else:
                            rx_stream_buffer = rx_stream_buffer[idx:]
                            break

                if was_lock and not dec.locked:
                    self.write_log("SYS: KİLİT KAYBI - Yeniden Aranıyor...")
                    self.locked = False
                    was_lock = False
                    expected_seq = -1
                    rx_stream_buffer.clear()

        sock.close()


if __name__ == '__main__':
    root = tk.Tk()
    app = GargaHILApp(root)
    root.mainloop()