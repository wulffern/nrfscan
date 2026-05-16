# nrfscan

BLE spectrum snapshot for nRF52833 DK.

Once per second (default) the firmware:

1. Starts HFCLK and sweeps **80** 1 MHz bins (`2400`–`2479` MHz, RADIO `FREQUENCY` 0–79)
2. Records the **maximum RSSI** (dBm) per channel during a short dwell time
3. Prints one JSON line on UART (115200, pins like [nrfdmiq initiator](../nrfdmiq/initiator/src/main.c): TX=6, RX=8)
4. Waits on RTC2 until the next 1 s period (low-power `WFE` between scans)

Each index `n` is **2400 + n** MHz (80 channels across the 2.4 GHz band).

## Build

Requires Nordic Connect SDK (tested with v3.3.0) and `west` on `PATH`.

```bash
make build
make flash
```

Override dwell and scan interval:

```bash
make build SCAN_DWELL_US=800 SCAN_INTERVAL_MS=2000
```

## JSON report

Example (one line terminated with `\n\r` like initiator):

```json
{"rssi_dB":[-96,-95,...],"dwell_us":400,"scan_duration_us":32000,"channels":80,"interval_ms":1000,"scan_count":3}
```

| Field | Meaning |
|-------|---------|
| `rssi_dB` | Max RSSI per channel, index = MHz − 2400 |
| `dwell_us` | Listen time per channel |
| `scan_duration_us` | Total RSSI sweep time (all 80 channels) |
| `channels` | Always 80 |
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

Stream and save under `data/`:

```bash
python3 py/read_log.py watch --com /dev/tty.usbmodemXXXX --save data/
```

Terminal bar chart from serial or a saved file (`--gui` for matplotlib):

```bash
python3 py/read_log.py plot --com /dev/tty.usbmodemXXXX
python3 py/read_log.py plot --file data/sample.json --bar-height 14
python3 py/read_log.py plot-dir data/ --latest
python3 py/read_log.py watch --com /dev/tty.usbmodemXXXX --save data/
```
