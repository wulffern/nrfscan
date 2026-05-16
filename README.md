# nrfscan

BLE spectrum snapshot for nRF52833 DK.

Once per second (default) the firmware:

1. Starts HFCLK and sweeps the 2.4 GHz band in **1 MHz** or **2 MHz** bins (see `SCAN_RADIO_2MBIT`)
2. Records **RSSI** (dBm) per bin after a short settle (`SCAN_SETTLE_US`)
3. Prints one JSON line on UART (115200, pins like [nrfdmiq initiator](../nrfdmiq/initiator/src/main.c): TX=6, RX=8)
4. Sleeps until RTC2 **CC[0]** compare fires after `SCAN_INTERVAL_MS` (default 1 s)

| `SCAN_RADIO_2MBIT` | PHY | Channels | Step | MHz (index `n`) |
|--------------------|-----|----------|------|-----------------|
| 1 (default) | BLE 2 Mbps | 40 | 2 MHz | 2400 + 2×n → 2400…2478 |
| 0 | BLE 1 Mbps | 80 | 1 MHz | 2400 + n → 2400…2479 |

Per bin: `FREQUENCY` → `RXEN` → settle → `RSSISTART` (BLE **2 Mbps**).
Wait for **READY** on bin 0 and every `SCAN_DISABLE_PERIOD_CH` bins after
`DISABLE` (`count_ready` ≈ `1 + (channels−1)/period`). Final `DISABLE` after the sweep.

## Build

Requires Nordic Connect SDK (tested with v3.3.0) and `west` on `PATH`.

```bash
make build
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

```bash
python3 py/read_log.py read --com /dev/tty.usbmodemXXXX
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
