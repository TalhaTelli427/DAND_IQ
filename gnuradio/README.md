# gnuradio

`dandiq_rx.grc` — the receiver for the Dand-IQ link. GNU Radio 3.10.

## Origin

This flowgraph is **not** original work. It comes from lesson 20 of Jason
Gallicchio's [learnSDR](https://gallicchio.github.io/learnSDR/) course,
*Resolving the Phase Ambiguity and Differential Encoding*, and keeps its
original `author` field. See [`LICENSE.learnSDR`](LICENSE.learnSDR) for the
upstream MIT notice — it is MIT, so reuse is fine, but the notice travels with
the file.

## Chain

```
bladeRF ──> AGC ──> FLL Band-Edge ──> Skip Head ──> Symbol Sync ──> Constellation
                                      (1 s)         (polyphase MF)   Receiver
                                                                        │
                          UDP 127.0.0.1:2345 <── Differential Decoder <─┘
```

`Constellation Receiver` carries its own Costas loop, so there is no separate
Costas block. `Symbol Sync` runs in `IR_PFB_MF` mode with 32 filter arms, doing
the matched filtering itself from `rcc_taps`. `Skip Head` drops the first second
of samples so the AGC and FLL have settled before the loops downstream see
anything.

## Parameters

| Variable | Value | Why |
|---|---|---|
| `samp_rate` | 200 kSps | 2 samples per symbol at 100 ksym/s |
| `sps` | 2 | — |
| `alpha` | 0.35 | matches the transmitter's RRC roll-off |
| `nfilts` | 32 | polyphase matched-filter arms |
| `center_freq` | 433 MHz | transmit frequency |
| tuned to | `center_freq + 1e6` | 1 MHz offset keeps the receiver's DC spike out of the signal |
| UDP out | `127.0.0.1:2345` | where the tools in [`../tools`](../tools) listen |

The source block is `osmosdr` with `args: bladerf=0`. For a different radio,
change those args and the gain fields; nothing else in the chain depends on the
hardware.

## Running

```bash
gnuradio-companion gnuradio/dandiq_rx.grc     # open, then Run
python tools/hil_gui.py                       # in parallel, to see decoded frames
```

The flowgraph only emits symbols. Frame sync, the dibit-mapping search and CRC
verification all happen host-side in `../tools`.
