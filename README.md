# nrfscan

BLE spectrum snapshot for **nRF52833 DK** and **nRF54L15 DK**.

Once per second (default) the firmware:

1. Starts HFCLK and sweeps the 2.4 GHz band in **1 MHz** or **2 MHz** bins (see `SCAN_RADIO_2MBIT`)
2. Records **RSSI** (dBm) per bin after a short settle (`SCAN_SETTLE_US`)
3. Prints one JSON line on UART (115200): nRF52 DK pins 6/8 (P0.06/P0.08); nRF54L15 DK **`NRF_UARTE20`** on P1.04/P1.05 (nrfx pin numbers 36/37) → J-Link VCOM
4. Sleeps until RTC2 **CC[0]** compare fires after `SCAN_INTERVAL_MS` (default 1 s)

| `SCAN_RADIO_2MBIT` | PHY | Channels | Step | MHz (index `n`) |
|--------------------|-----|----------|------|-----------------|
| 1 (default) | BLE 2 Mbps | 40 | 2 MHz | 2400 + 2×n → 2400…2478 |
| 0 | BLE 1 Mbps | 80 | 1 MHz | 2400 + n → 2400…2479 |

Per bin: `FREQUENCY` → settle → `RSSISTART` (BLE **2 Mbps**).

| SoC | Sweep |
|-----|--------|
| nRF54L15 / LM20 | HFCLK via `z_nrf_clock_bt_ctlr_hf_request()` (same as BLE LL). Per bin: `FREQUENCY` → `PLLEN` → `PLLREADY` → `RXEN` → `RXREADY` → `START` → RSSI. BLE 2M + DTM-style PCNF (no whitening). |
| nRF52833 | Same, plus periodic `DISABLE`+`RXEN` every `SCAN_DISABLE_PERIOD_CH` bins |

## Build

Requires Nordic Connect SDK (tested with v3.3.0) and `west` on `PATH`.

```bash
# nRF52833 DK (default)
make build
make flash

# nRF54L15 DK
make build-54l15
make flash
```

Build-time options (also echoed in JSON):

| Variable | Default | Meaning |
|----------|---------|---------|
| `SCAN_SETTLE_US` | 10 | µs after RXEN before RSSI |
| `SCAN_DISABLE_PERIOD_CH` | 10 | `DISABLE`+`READY` every N bins |
| `SCAN_RADIO_2MBIT` | 1 | 1 = 2 Mbps / 40×2 MHz bins; 0 = 1 Mbps / 80×1 MHz bins |
| `SCAN_INTERVAL_MS` | 1000 | ms between sweeps |

```bash
make build SCAN_DISABLE_PERIOD_CH=20 SCAN_RADIO_2MBIT=0 SCAN_SETTLE_US=10
```

## JSON report

Example (one line terminated with `\n\r` like initiator):

```json
{"rssi_dB":[-96,-95,...],"scan_duration_us":16000,"channels":40,"freq_base_mhz":2400,"freq_step_mhz":2,"interval_ms":1000,"scan_count":3}
```

| Field | Meaning |
|-------|---------|
| `rssi_dB` | Max RSSI per bin; MHz = `freq_base_mhz` + index × `freq_step_mhz` |
| `scan_duration_us` | RSSI sweep with HFCLK on (TIMER1, excludes HFCLK startup) |
| `time_hfclk_us` | HFCLK startup before the sweep |
| `time_ready_us` | Waiting for RADIO `READY` (first bin + each disable hop) |
| `time_disable_us` | `DISABLE` → `DISABLED` → auto `RXEN` hops |
| `time_settle_us` | Busy-wait after RX (`SCAN_SETTLE_US` per bin) |
| `time_rssi_us` | `RSSISTART` → `RSSIEND` per bin |
| `time_final_disable_us` | Radio off after the last bin |
| `time_other_us` | `scan_duration_us` minus the sum of the sweep phases above |
| `count_ready` / `count_disable` | How often each path ran |
| `channels` | Bin count (40 or 80) |
| `freq_base_mhz` | First bin frequency (2400) |
| `freq_step_mhz` | Spacing between bins (1 or 2) |
| `radio_2mbit` | Same as `SCAN_RADIO_2MBIT` build flag |
| `interval_ms` | Time between scan starts (default 1000) |
| `scan_count` | Increments each sweep |

## Two-DK RSSI test (`tx/`)

A minimal **carrier source** (`nrfscan_tx`, `TXEN` only — no packets) sweeps the same
band as the scanner (**1 MHz** or **2 MHz** steps per `TX_RADIO_2MBIT`), **1 s** per channel.

TX defaults to **BLE 1 Mbps** (`TX_RADIO_2MBIT=0`). Match the scanner PHY, e.g.
`make build SCAN_RADIO_2MBIT=0` and `make tx-build TX_RADIO_2MBIT=0`.

```bash
# DK 1 — transmitter (no UART needed)
make tx-build tx-flash

# DK 2 — scanner (UART to host)
make build flash
python3 py/read_log.py watch --com /dev/tty.usbmodemXXXX
```

Override sweep range or step time:

```bash
make build SCAN_RADIO_2MBIT=0
make tx-build TX_RADIO_2MBIT=0 TX_DWELL_MS=1000
```

## Host

Install Python dependencies:

```bash
python3 -m pip install -r requirements.txt
```

Read one report (summary table + terminal bar chart, ~1 s between lines):

On macOS use **`/dev/cu.usbmodem…`** (not `tty.usbmodem…`). The J-Link **SID** is only the serial
number (e.g. `001057706325`); macOS adds a trailing **interface digit** (`…3251`, `…3253`) for each
VCOM. Your DK exposes two interfaces for one SID.

```bash
python3 py/read_log.py list-ports --sid 001057706325
python3 py/read_log.py watch --sid 001057706325 --autodetect
python3 py/read_log.py read --com /dev/cu.usbmodem0010577063251
python3 py/read_log.py plot --file data/sample.json
python3 py/read_log.py read --com /dev/tty.usbmodemXXXX --no-bars   # table only
```

Stream and save under `data/` (bar chart shows **█** current sweep and **▲**
session max per MHz; enabled by default):

```bash
python3 py/read_log.py watch --com /dev/tty.usbmodemXXXX --save data/
python3 py/read_log.py plot-dir data/ --track-max
```

Terminal bar chart from serial or a saved file (`--gui` for matplotlib):

```bash
python3 py/read_log.py plot --com /dev/tty.usbmodemXXXX
python3 py/read_log.py plot --file data/sample.json --bar-height 14
python3 py/read_log.py plot-dir data/ --latest
python3 py/read_log.py watch --com /dev/tty.usbmodemXXXX --save data/
```
