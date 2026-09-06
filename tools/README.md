# tools

Host-side utilities for bringing up and measuring the Dand-IQ link.

| Tool | What it does |
|------|--------------|
| `hil_gui.py` | Hardware-in-the-loop testbench with a live dashboard: drives the transmitter over the board's serial port with numbered packets, takes the demodulated symbols back over UDP, and shows lock state, throughput and packet loss. |
| `decoder.py` | The same loop on the command line, with CRC32 verification. Listen-only, or `--tx-port` to also generate the test traffic. |
| `ber.py` | BER / PER measurement from the UDP symbol stream — the tool behind the figures in the top-level README. |
| `analyze.py` | Decodes a logic-analyzer capture of the raw 1-bit ΣΔ stream (CLK + I_OUT + Q_OUT) via FFT. Finds the clock channel, sample rate and channel order on its own — receiver verification without an FPGA. |
| `watch.py` | Watches the folder and re-runs `analyze.py` whenever a new CSV export appears. |

```bash
pip install pyserial numpy pandas matplotlib

python hil_gui.py                                   # GUI testbench
python decoder.py --port 2345 --crc                 # CLI decoder, listen only
python decoder.py --tx-port COM3 --tx-baud 115200   # CLI decoder, drives TX too
python ber.py --port 2345 --crc --istatistik        # BER / PER
python analyze.py                                   # newest CSV in this folder
```

All of them expect the demodulated symbol stream on UDP `127.0.0.1:2345`, which is
where the GNU Radio receiver's `UDP Sink` puts it.

Common frame parameters (`0x65 0x65` sync, 3-byte header, 32-byte payload, 4-byte
CRC32) are duplicated at the top of each script — change them together.
