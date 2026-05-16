# nrfscan

BLE spectrum snapshot for nRF52833 DK.

Once per second (default) the firmware:

1. Starts HFCLK and sweeps **40** 2 MHz bins (`2400`–`2478` MHz, RADIO `FREQUENCY` 0, 2, … 78)
2. Records the **maximum RSSI** (dBm) per bin during a short dwell time
3. Prints one JSON line on UART (115200, pins like [nrfdmiq initiator](../nrfdmiq/initiator/src/main.c): TX=6, RX=8)
4. Waits on RTC2 until the next 1 s period (low-power `WFE` between scans)

Each index `n` is **2400 + 2×n** MHz (40 bins across the 2.4 GHz band).

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
| `SCAN_DISABLE_PERIOD_CH` | 10 | `DISABLE`+`READY` every N bins (40 bins → 4 READY waits; was 2 → 20) |
| `SCAN_RADIO_2MBIT` | 1 | 1 = BLE 2 Mbps, 0 = 1 Mbps |
| `SCAN_INTERVAL_MS` | 1000 | ms between sweeps |
| `SCAN_DWELL_US` | 5 | legacy field in JSON |

```bash
make build SCAN_DISABLE_PERIOD_CH=20 SCAN_RADIO_2MBIT=0 SCAN_SETTLE_US=10
```

## JSON report

Example (one line terminated with `\n\r` like initiator):

```json
{"rssi_dB":[-96,-95,...],"dwell_us":400,"scan_duration_us":16000,"channels":40,"freq_base_mhz":2400,"freq_step_mhz":2,"interval_ms":1000,"scan_count":3}
```

| Field | Meaning |
|-------|---------|
| `rssi_dB` | Max RSSI per bin; MHz = `freq_base_mhz` + index × `freq_step_mhz` |
| `dwell_us` | Listen time per bin |
| `scan_duration_us` | RSSI sweep with HFCLK on (TIMER1, excludes HFCLK startup) |
| `time_hfclk_us` | HFCLK startup before the sweep |
| `time_ready_us` | Waiting for RADIO `READY` (first bin + each disable hop) |
| `time_disable_us` | `DISABLE` → `DISABLED` → auto `RXEN` hops |
| `time_settle_us` | Busy-wait after RX (`SCAN_SETTLE_US` per bin) |
| `time_rssi_us` | `RSSISTART` → `RSSIEND` per bin |
| `time_final_disable_us` | Radio off after the last bin |
| `time_other_us` | `scan_duration_us` minus the sum of the sweep phases above |
| `count_ready` / `count_disable` | How often each path ran |
| `channels` | Bin count (40) |
| `freq_base_mhz` | First bin frequency (2400) |
| `freq_step_mhz` | Spacing between bins (2) |
| `interval_ms` | Time between scan starts (default 1000) |
| `scan_count` | Increments each sweep |

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
