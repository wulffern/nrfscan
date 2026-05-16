#!/usr/bin/env python3
######################################################################
##  Read JSON spectrum reports from nrfscan over UART or from files.
######################################################################

from __future__ import annotations

import datetime
import glob
import json
import os
import time
from typing import Any, Optional

import click
import matplotlib.pyplot as plt
import numpy as np
import serial
from rich.console import Console
from rich.table import Table
from rich.text import Text

console = Console()

_BAR_HEIGHT_DEFAULT = 10
_BAR_CEIL_DBM = -20
_BAR_FLOOR_DBM = -90

RSSI_CHANNELS = 80
RSSI_FREQ_BASE_MHZ = 2400


def channel_mhz(ch: int) -> float:
    """Match hal_radio.c: 1 MHz bins, FREQUENCY register 0..79 -> 2400..2479 MHz."""
    return RSSI_FREQ_BASE_MHZ + ch


def channel_mhz_array(count: int = RSSI_CHANNELS) -> np.ndarray:
    return np.array([channel_mhz(ch) for ch in range(count)], dtype=float)


def _normalize_line(raw: bytes) -> bytes:
    line = raw.strip()
    if line.endswith(b"\r"):
        line = line[:-1]
    return line


def parse_json_line(raw: bytes) -> dict[str, Any]:
    line = _normalize_line(raw)
    if not line:
        raise ValueError("empty line")
    return json.loads(line.decode("utf-8"))


class ScanReport:
    def __init__(self, obj: Optional[dict[str, Any]] = None) -> None:
        self.obj: dict[str, Any] = obj or {}
        self.filename: Optional[str] = None
        self.rssi_db: np.ndarray = np.array([], dtype=int)
        self.dwell_us: int = -1
        self.duration_us: int = -1
        self.channels: int = RSSI_CHANNELS
        self.scan_duration_us: int = -1
        self.interval_ms: int = -1
        self.scan_count: int = -1
        self.sleep_sec: int = -1
        self.wake_count: int = -1

    @classmethod
    def from_file(cls, path: str) -> "ScanReport":
        r = cls()
        r.filename = path
        with open(path, encoding="utf-8") as fi:
            r.obj = json.load(fi)
        r.load()
        return r

    @classmethod
    def from_com(cls, port: str, timeout: float = 30.0) -> "ScanReport":
        r = cls()
        r.read_from_com(port, timeout=timeout)
        r.load()
        return r

    def read_from_com(self, port: str, timeout: float = 30.0) -> None:
        with serial.Serial(port, baudrate=115200, timeout=timeout) as ser:
            raw = ser.readline()
        self.obj = parse_json_line(raw)

    def save(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as fo:
            json.dump(self.obj, fo)

    def load(self) -> None:
        self.rssi_db = np.array(self.obj["rssi_dB"], dtype=int)
        self.dwell_us = int(self.obj.get("dwell_us", -1))
        if "scan_duration_us" in self.obj:
            self.scan_duration_us = int(self.obj["scan_duration_us"])
        else:
            self.scan_duration_us = int(self.obj.get("duration_us", -1))
        self.duration_us = self.scan_duration_us
        self.channels = int(self.obj.get("channels", len(self.rssi_db)))
        if "interval_ms" in self.obj:
            self.interval_ms = int(self.obj["interval_ms"])
        elif "sleep_sec" in self.obj:
            self.interval_ms = int(self.obj["sleep_sec"]) * 1000
        else:
            self.interval_ms = -1
        if "scan_count" in self.obj:
            self.scan_count = int(self.obj["scan_count"])
        elif "wake_count" in self.obj:
            self.scan_count = int(self.obj["wake_count"])
        else:
            self.scan_count = -1
        self.sleep_sec = int(self.obj.get("sleep_sec", -1))
        self.wake_count = int(self.obj.get("wake_count", -1))

    @property
    def mhz(self) -> np.ndarray:
        n = len(self.rssi_db)
        return channel_mhz_array(n)

    def peak(self) -> tuple[int, int, float]:
        """Return (channel, rssi_dBm, mhz) of strongest reading."""
        ch = int(np.argmax(self.rssi_db))
        return ch, int(self.rssi_db[ch]), channel_mhz(ch)

    def _rssi_bar_level(self, dbm: float, height: int) -> int:
        """Map dBm to bar row using fixed axis [_BAR_FLOOR_DBM, _BAR_CEIL_DBM]."""
        clamped = max(_BAR_FLOOR_DBM, min(_BAR_CEIL_DBM, dbm))
        span = _BAR_CEIL_DBM - _BAR_FLOOR_DBM
        if height <= 1:
            return 0
        return int(round((clamped - _BAR_FLOOR_DBM) / span * (height - 1)))

    def print_bar_chart(self, height: int = _BAR_HEIGHT_DEFAULT) -> None:
        """Vertical bar chart in the terminal (one column per MHz bin)."""
        rssi = self.rssi_db
        n = len(rssi)
        if n == 0:
            return

        peak_ch = int(np.argmax(rssi))
        height = max(4, height)
        span = _BAR_CEIL_DBM - _BAR_FLOOR_DBM

        console.print(
            f"[bold]RSSI spectrum[/]  "
            f"scan={self.scan_count}  "
            f"[dim]{int(self.mhz[0])}–{int(self.mhz[-1])} MHz  "
            f"{self.scan_duration_us} us  "
            f"axis {_BAR_CEIL_DBM}..{_BAR_FLOOR_DBM} dBm[/]"
        )

        for row in range(height - 1, -1, -1):
            tick_dbm = _BAR_FLOOR_DBM + (row / (height - 1)) * span if height > 1 else _BAR_FLOOR_DBM
            line = Text(f"{tick_dbm:4.0f} │")
            for i in range(n):
                v = float(rssi[i])
                level = self._rssi_bar_level(v, height)
                if level >= row:
                    if i == peak_ch:
                        line.append("█", style="bold yellow")
                    elif v >= -35:
                        line.append("█", style="bold green")
                    elif v >= -50:
                        line.append("█", style="green")
                    else:
                        line.append("█", style="cyan")
                else:
                    line.append(" ")
            console.print(line, highlight=False)

        axis = Text("     └" + "─" * n)
        console.print(axis, highlight=False)

        # MHz labels under the chart (start, mid ticks, end)
        label_row = Text("      ")
        ticks_mhz = [int(self.mhz[0]), int(self.mhz[n // 4]), int(self.mhz[n // 2]),
                     int(self.mhz[3 * n // 4]), int(self.mhz[-1])]
        tick_cols = [0, n // 4, n // 2, 3 * n // 4, n - 1]
        pos = 0
        for col, mhz_val in zip(tick_cols, ticks_mhz):
            label = str(mhz_val)
            if col > pos:
                label_row.append(" " * (col - pos))
                pos = col
            label_row.append(label, style="dim")
            pos += len(label)
        console.print(label_row, highlight=False)
        console.print(
            f"      [dim]peak {int(self.mhz[peak_ch])} MHz  "
            f"{int(rssi[peak_ch])} dBm[/]",
            highlight=False,
        )

    def print_summary(self, show_bars: bool = True, bar_height: int = _BAR_HEIGHT_DEFAULT) -> None:
        ch, rssi, mhz = self.peak()
        table = Table(title="nrfscan report", show_header=True)
        table.add_column("field", style="cyan")
        table.add_column("value", justify="right")
        table.add_row("scan_count", str(self.scan_count))
        table.add_row("channels", str(self.channels))
        table.add_row("dwell_us", str(self.dwell_us))
        table.add_row("scan_duration_us", str(self.scan_duration_us))
        table.add_row("interval_ms", str(self.interval_ms))
        table.add_row("peak channel", str(ch))
        table.add_row("peak MHz", f"{mhz:.0f}")
        table.add_row("peak rssi_dB", str(rssi))
        table.add_row("rssi min", str(int(self.rssi_db.min())))
        table.add_row("rssi max", str(int(self.rssi_db.max())))
        console.print(table)
        if show_bars:
            console.print()
            self.print_bar_chart(height=bar_height)

    def plot(self, title: Optional[str] = None) -> None:
        mhz = self.mhz
        rssi = self.rssi_db
        fig, ax = plt.subplots(figsize=(12, 4))
        ax.plot(mhz, rssi, color="steelblue", linewidth=1.2)
        ax.scatter(mhz, rssi, s=12, color="steelblue", zorder=3)
        for adv_mhz in (2402, 2426, 2480):
            ax.axvline(adv_mhz, color="orange", alpha=0.35, linestyle="--")
        ax.set_xlabel("Frequency [MHz]")
        ax.set_ylabel("RSSI [dBm]")
        ax.set_title(title or f"scan={self.scan_count}  dwell={self.dwell_us} us")
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        plt.show()


@click.group()
def cli() -> None:
    """Read and plot nrfscan UART JSON logs."""


@cli.command()
@click.option("--com", required=True, help="Serial port (e.g. /dev/tty.usbmodem…)")
@click.option("--timeout", default=30.0, show_default=True, help="Seconds to wait for next report")
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
def read(com: str, timeout: float, no_bars: bool, bar_height: int) -> None:
    """Read one report from the device and print a summary."""
    r = ScanReport.from_com(com, timeout=timeout)
    r.print_summary(show_bars=not no_bars, bar_height=bar_height)


@cli.command()
@click.option("--com", required=True, help="Serial port")
@click.option(
    "--save",
    "save_dir",
    default=None,
    type=click.Path(file_okay=False),
    help="Directory for timestamped .json files",
)
@click.option("--count", default=0, show_default=True, help="Reports to read (0 = forever)")
@click.option("--timeout", default=30.0, show_default=True, help="Per-read serial timeout [s]")
@click.option("--plot", "do_plot", is_flag=True, help="Show matplotlib window for each report")
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
def watch(com: str, save_dir: Optional[str], count: int, timeout: float, do_plot: bool,
          no_bars: bool, bar_height: int) -> None:
    """Stream reports as the device wakes and transmits."""
    if save_dir and not os.path.isdir(save_dir):
        os.makedirs(save_dir)

    n = 0
    with serial.Serial(com, baudrate=115200, timeout=timeout) as ser:
        while count == 0 or n < count:
            try:
                raw = ser.readline()
                r = ScanReport(parse_json_line(raw))
                r.load()
            except (json.JSONDecodeError, ValueError, UnicodeDecodeError) as exc:
                console.print(f"[red]skip:[/] {exc!r}  raw={raw!r}")
                continue

            n += 1
            console.rule(f"report {n}")
            r.print_summary(show_bars=not no_bars, bar_height=bar_height)

            if save_dir:
                fname = f"{datetime.datetime.now().timestamp():.6f}.json"
                path = os.path.join(save_dir, fname)
                r.save(path)
                console.print(f"saved [dim]{path}[/]")

            if do_plot:
                r.plot()


@cli.command()
@click.option("--file", "filename", default=None, type=click.Path(dir_okay=False))
@click.option("--com", default=None, help="Serial port (reads one report)")
@click.option("--timeout", default=30.0, show_default=True)
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
@click.option("--gui", is_flag=True, help="Open matplotlib window")
def plot(filename: Optional[str], com: Optional[str], timeout: float,
         no_bars: bool, bar_height: int, gui: bool) -> None:
    """Show RSSI spectrum from a file or one live reading."""
    if com:
        r = ScanReport.from_com(com, timeout=timeout)
    elif filename:
        r = ScanReport.from_file(filename)
    else:
        raise click.UsageError("Provide --file or --com")
    r.print_summary(show_bars=not no_bars, bar_height=bar_height)
    if gui:
        r.plot(title=os.path.basename(filename) if filename else None)


@cli.command("plot-dir")
@click.argument("dirname", type=click.Path(exists=True, file_okay=False))
@click.option("--latest", is_flag=True, help="Only show the newest file")
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
@click.option("--gui", is_flag=True, help="Open matplotlib window")
def plot_dir(dirname: str, latest: bool, no_bars: bool, bar_height: int, gui: bool) -> None:
    """Show saved JSON spectrum reports from a directory."""
    files = sorted(glob.glob(os.path.join(dirname, "*.json")))
    if not files:
        console.print(f"[yellow]no .json files in {dirname}[/]")
        return
    if latest:
        files = [files[-1]]
    for path in files:
        r = ScanReport.from_file(path)
        console.rule(os.path.basename(path))
        r.print_summary(show_bars=not no_bars, bar_height=bar_height)
        if gui:
            r.plot(title=os.path.basename(path))


if __name__ == "__main__":
    cli()
