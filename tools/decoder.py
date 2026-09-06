#!/usr/bin/env python3
"""
Garga Labs - Dand-IQ HIL (Hardware-in-the-Loop) Testbench
=========================================================

Kullanim:
    1. Sadece Dinleme Modu:
       python decoder.py --sabit

    2. Otomatik HIL Test Modu (UART'tan basar, UDP'den dogrular ve LOG tutar):
       python decoder.py --tx-port COM3 --tx-baud 115200
"""

import argparse
import socket
import sys
import time
import datetime
import zlib
import threading
import serial
from collections import deque
from itertools import permutations

# ----------------------------------------------------------------------
# Cerceve Parametreleri
# ----------------------------------------------------------------------
SYNC = bytes([0x65, 0x65])
HDR_LEN = 3
PAYLOAD_LEN = 32
CRC_LEN = 4

SYNC_BITS = ''.join(f'{b:08b}' for b in SYNC)

# ----------------------------------------------------------------------
# Loglama Altyapisi (Thread-Safe)
# ----------------------------------------------------------------------
LOG_LOCK = threading.Lock()
LOG_FILE = None


def write_log(event_type, seq=0, message=""):
    """
    Karsilastirmali HIL loglarini dosyaya yazar.
    Format: [SAAT] | YON | SIRA NO | MESAJ
    """
    if LOG_FILE is None:
        return

    t_stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    with LOG_LOCK:
        if event_type == "TX":
            LOG_FILE.write(f"[{t_stamp}] | TX  | SEQ: {seq:08d} | Gonderildi\n")
        elif event_type == "RX":
            LOG_FILE.write(f"[{t_stamp}] | RX  | SEQ: {seq:08d} | {message}\n")
        elif event_type == "SYS":
            LOG_FILE.write(f"[{t_stamp}] | SYS | -------- | {message}\n")

        LOG_FILE.flush()  # Dosyayi aninda diske yaz


# ----------------------------------------------------------------------
# Yardimci Fonksiyonlar
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
        if len(chunk) < 8:
            break
        out.append(int(chunk, 2))
    return bytes(out)


# ----------------------------------------------------------------------
# Kilit ve Permutasyon Arama
# ----------------------------------------------------------------------
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

        if len(hits) < 3:
            return 0, None

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
            if cnt > best[0]:
                best = (cnt, p, off)
        return best


# ----------------------------------------------------------------------
# Cerceve Cozucu
# ----------------------------------------------------------------------
class FrameDecoder:
    def __init__(self, use_crc=False):
        self.use_crc = use_crc
        self.frame_len = HDR_LEN + PAYLOAD_LEN + (CRC_LEN if use_crc else 0)
        self.finder = PermFinder(self.frame_len)

        self.perm = None
        self.offset = None
        self.locked = False
        self.bit_buffer = ""

        self.n_frames = 0
        self.n_sync_err = 0
        self.rx_bytes = 0

    def try_lock(self, symbols):
        score, perm, off = self.finder.find(symbols)
        if score >= 3:
            self.perm = perm
            self.offset = off
            self.locked = True
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
            self.n_frames += 1

            if frame[0] != 0x65 or frame[1] != 0x65:
                self.n_sync_err += 1
                if self.n_sync_err > 20:
                    self.locked = False
                    self.bit_buffer = ""
                    break
                continue

            self.n_sync_err = 0

            n = frame[2]
            if n > PAYLOAD_LEN:
                self.n_sync_err += 1
                continue

            if n > 0:
                self.rx_bytes += n
                out.append(frame[HDR_LEN:HDR_LEN + n])

        return out


# ----------------------------------------------------------------------
# HIL Test Transmitter (UART Uzerinden)
# ----------------------------------------------------------------------
class UartTester(threading.Thread):
    def __init__(self, port, baudrate, interval=0.1):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.interval = interval
        self.daemon = True
        self.tx_count = 0
        self.running = False
        self.serial_conn = None

    def run(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            self.running = True
            while self.running:
                msg = f"GRGA{self.tx_count:08d}"
                payload = msg.encode('utf-8').ljust(PAYLOAD_LEN, b'.')

                # Veriyi bas ve Logla
                self.serial_conn.write(payload)
                write_log("TX", self.tx_count)

                self.tx_count += 1
                time.sleep(self.interval)
        except Exception as e:
            print(f"\n[HATA] UART Portu acilamadi ({self.port}): {e}", file=sys.stderr)
            self.running = False


# ----------------------------------------------------------------------
# Ana Dongu
# ----------------------------------------------------------------------
def main():
    global LOG_FILE

    ap = argparse.ArgumentParser(description="Garga Labs Dand-IQ HIL Testbench")
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=2345)
    ap.add_argument('--crc', action='store_true', help='CRC32 bekle')
    ap.add_argument('--sabit', action='store_true', help='Terminalde ayni satira yazar')
    ap.add_argument('--tx-port', type=str, help='Test verisi gonderilecek COM port (or: COM3, /dev/ttyUSB0)')
    ap.add_argument('--tx-baud', type=int, default=115200, help='UART Baudrate')
    args = ap.parse_args()

    dec = FrameDecoder(use_crc=args.crc)

    tester = None
    test_mode = False
    rx_valid_count = 0
    rx_missed_count = 0
    expected_seq = -1
    rx_stream_buffer = bytearray()

    if args.tx_port:
        test_mode = True
        # Log dosyasini olustur
        log_filename = f"garga_hil_log_{time.strftime('%Y%m%d_%H%M%S')}.txt"
        LOG_FILE = open(log_filename, "w", encoding="utf-8")
        write_log("SYS", message=f"Test Baslatildi. TX Port: {args.tx_port}, Hedef: {args.host}:{args.port}")

        tester = UartTester(port=args.tx_port, baudrate=args.tx_baud, interval=0.05)
        tester.start()
        time.sleep(0.5)
        if tester.running:
            print(f"[TEST MODU] {args.tx_port} uzerinden otomatik veri basiliyor...")
            print(f"[TEST MODU] Karsilastirmali log dosyasi acildi: {log_filename}")
        else:
            sys.exit(1)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind((args.host, args.port))
    sock.settimeout(0.5)

    print(f"Dinleniyor : {args.host}:{args.port}")
    print("Kilit araniyor...\n")

    buf = bytearray()
    t0 = time.time()
    t_last = t0
    was_lock = False

    try:
        while True:
            try:
                pkt = sock.recv(65536)
                buf.extend(pkt)
            except socket.timeout:
                pass

            # ---- KILIT ARAMA ----
            if not dec.locked and len(buf) >= 4000:
                ok, score = dec.try_lock(buf[:4000])
                if ok:
                    print(f"\n[SISTEM] KILITLENDI! offset={dec.offset} bit", file=sys.stderr)
                    write_log("SYS", message=f"KILITLENDI. offset={dec.offset} bit")
                    was_lock = True
                else:
                    buf = buf[-2000:]

            # ---- VERI COZME VE HIL KONTROLU ----
            if dec.locked and len(buf) > 0:
                blocks = dec.parse(buf)
                buf = bytearray()

                for b in blocks:
                    if test_mode:
                        rx_stream_buffer.extend(b)

                        while True:
                            idx = rx_stream_buffer.find(b'GRGA')
                            if idx == -1:
                                rx_stream_buffer = rx_stream_buffer[-3:]
                                break

                            if len(rx_stream_buffer) >= idx + PAYLOAD_LEN:
                                packet = rx_stream_buffer[idx: idx + PAYLOAD_LEN]
                                rx_stream_buffer = rx_stream_buffer[idx + PAYLOAD_LEN:]

                                try:
                                    seq = int(packet[4:12].decode('utf-8'))
                                    if expected_seq != -1 and seq != expected_seq:
                                        missed = seq - expected_seq
                                        if missed > 0:
                                            rx_missed_count += missed
                                            write_log("RX", seq,
                                                      f"HATA - {missed} Paket Atlandi! (Beklenen: {expected_seq:08d})")
                                        else:
                                            write_log("RX", seq, "HATA - Sira Disi (Gecikmeli) Paket")
                                    else:
                                        write_log("RX", seq, "BASARILI")

                                    rx_valid_count += 1
                                    expected_seq = seq + 1
                                except ValueError:
                                    write_log("RX", 0, f"HATA - Bozuk Format: {packet.hex()}")
                            else:
                                rx_stream_buffer = rx_stream_buffer[idx:]
                                break
                    else:
                        text = b.decode('utf-8', errors='replace')
                        if args.sabit:
                            text = text.replace('\n', '').replace('\r', '')
                            sys.stdout.write('\r' + text.ljust(80))
                        else:
                            sys.stdout.write(text)
                        sys.stdout.flush()

                if was_lock and not dec.locked:
                    print("\n[SISTEM] KILIT KAYBI - yeniden araniyor", file=sys.stderr)
                    write_log("SYS", message="KILIT KAYBI. Yeniden senkron araniyor...")
                    was_lock = False
                    expected_seq = -1
                    rx_stream_buffer.clear()

            # ---- CANLI DASHBOARD ----
            now = time.time()
            if test_mode and (now - t_last >= 0.5):
                tx_tot = tester.tx_count if tester else 0

                if tx_tot > 0:
                    reliability = (rx_valid_count / tx_tot) * 100
                    if reliability > 100: reliability = 100.0
                else:
                    reliability = 0.0

                lock_stat = "KILITLI" if dec.locked else "ARANIYOR"

                dashboard = f"\r[GARGA LABS HIL] TX: {tx_tot:05d} | RX OK: {rx_valid_count:05d} | KAYIP: {rx_missed_count:04d} | GUVENILIRLIK: %{reliability:05.2f} | DURUM: {lock_stat}"
                sys.stdout.write(dashboard.ljust(90))
                sys.stdout.flush()
                t_last = now

    except KeyboardInterrupt:
        print("\n\nTest sonlandiriliyor...", file=sys.stderr)
        write_log("SYS", message="Test kullanici tarafindan durduruldu.")
        if test_mode and tester:
            tester.running = False
        if LOG_FILE:
            LOG_FILE.close()


if __name__ == '__main__':
    main()