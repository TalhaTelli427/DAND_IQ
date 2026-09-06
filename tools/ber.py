#!/usr/bin/env python3
"""
Dand-IQ telemetri cozucu
========================

Kullanim:
    python decoder.py                 # Sadece UART verisini gosterir
    python decoder.py --sabit         # Veriyi alt alta kaydirmadan hep ayni satirda gosterir
    python decoder.py --istatistik    # 2 saniyede bir baglanti kalitesini basar
"""

import argparse
import socket
import sys
import time
import zlib
from collections import deque
from itertools import permutations

SYNC = bytes([0x65, 0x65])
HDR_LEN = 3
PAYLOAD_LEN = 32
CRC_LEN = 4

SYNC_BITS = ''.join(f'{b:08b}' for b in SYNC)


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
        self.n_crc_ok = 0
        self.n_crc_bad = 0
        self.n_data = 0
        self.n_idle = 0
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

            if self.use_crc:
                body = frame[:HDR_LEN + PAYLOAD_LEN]
                rx = int.from_bytes(frame[-CRC_LEN:], 'little')
                if zlib.crc32(body) & 0xFFFFFFFF != rx:
                    self.n_crc_bad += 1
                    continue
                self.n_crc_ok += 1

            n = frame[2]
            if n > PAYLOAD_LEN:
                self.n_sync_err += 1
                continue

            if n == 0:
                self.n_idle += 1
            else:
                self.n_data += 1
                self.rx_bytes += n
                out.append(frame[HDR_LEN:HDR_LEN + n])

        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=2345)
    ap.add_argument('--crc', action='store_true', help='CRC32 bekle')
    ap.add_argument('--istatistik', action='store_true', help='2 saniyede bir frame durumu goster')
    ap.add_argument('--sabit', action='store_true', help='Veriyi alt alta kaydirmak yerine hep ayni satirda gosterir')
    args = ap.parse_args()

    dec = FrameDecoder(use_crc=args.crc)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind((args.host, args.port))
    sock.settimeout(1.0)

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

            if not dec.locked and len(buf) >= 4000:
                ok, score = dec.try_lock(buf[:4000])
                if ok:
                    # Kilit logunu stderr'e basiyoruz ki asil veriyle karismasin
                    print(f"\n[SISTEM] KILITLENDI! offset={dec.offset} bit", file=sys.stderr)
                    was_lock = True
                else:
                    buf = buf[-2000:]

            if dec.locked and len(buf) > 0:
                blocks = dec.parse(buf)
                buf = bytearray()

                for b in blocks:
                    text = b.decode('utf-8', errors='replace')
                    if args.sabit:
                        # Satir atlamalarini silip basa donme (\r) ekler
                        text = text.replace('\n', '').replace('\r', '')
                        sys.stdout.write('\r' + text.ljust(80))  # Ekrani temizleyip yazar
                    else:
                        sys.stdout.write(text)

                    sys.stdout.flush()

                if was_lock and not dec.locked:
                    print("\n[SISTEM] KILIT KAYBI - yeniden araniyor", file=sys.stderr)
                    was_lock = False
                    dec.n_sync_err = 0

            # Eger istatistik istenmisse goster
            now = time.time()
            if args.istatistik and (now - t_last >= 2.0):
                el = now - t0
                rate = dec.rx_bytes / el if el > 0 else 0
                print(f"\n[ cerceve {dec.n_frames} | hata {dec.n_sync_err} | {rate:.0f} B/s ]", file=sys.stderr)
                t_last = now

    except KeyboardInterrupt:
        print("\n\nCikis yapiliyor...", file=sys.stderr)


if __name__ == '__main__':
    main()