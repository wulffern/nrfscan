#!/usr/bin/env python3
######################################################################
##  Read JSON spectrum reports from nrfscan over UART or from files.
######################################################################

from __future__ import annotations

import datetime
import glob
import json
import os
import sys
import time
from typing import Any, Optional

import click
import matplotlib.pyplot as plt
import numpy as np
import serial
import serial.tools.list_ports
from rich.console import Console
from rich.table import Table
from rich.text import Text

console = Console()

_BAR_HEIGHT_DEFAULT = 10
_BAR_CEIL_DBM = -20
_BAR_FLOOR_DBM = -90

RSSI_CHANNELS_2M = 40
RSSI_CHANNELS_1M = 80
RSSI_FREQ_BASE_MHZ = 2400
RSSI_FREQ_STEP_2M = 2
RSSI_FREQ_STEP_1M = 1
# Terminal bar chart and plot use this many columns (1 MHz spacing at 80 bins).
DISPLAY_COLUMNS = 80
_RSSI_MIN = -127


def channel_mhz(ch: int, step_mhz: int) -> float:
    """Match hal_radio.h: index ch -> 2400 + ch * step_mhz."""
    return RSSI_FREQ_BASE_MHZ + ch * step_mhz


def channel_mhz_array(count: int, step_mhz: int) -> np.ndarray:
    return np.array([channel_mhz(ch, step_mhz) for ch in range(count)], dtype=float)


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


def _cu_device(device: str) -> str:
    """On macOS, prefer /dev/cu.* over /dev/tty.* for client access."""
    if sys.platform == "darwin" and device.startswith("/dev/tty."):
        cu = "/dev/cu." + device[len("/dev/tty.") :]
        if os.path.exists(cu):
            return cu
    return device


def ports_for_sid(sid: str) -> list[str]:
    """All VCOM devices for a J-Link serial number (SID)."""
    found: list[str] = []
    for info in serial.tools.list_ports.comports():
        if info.serial_number == sid:
            found.append(_cu_device(info.device))
    if not found:
        for info in serial.tools.list_ports.comports():
            if sid in (info.device or ""):
                found.append(_cu_device(info.device))
    return list(dict.fromkeys(found))


def autodetect_nrfscan_port(sid: str, probe_timeout: float = 3.0) -> str:
    """Return the VCOM that is sending nrfscan JSON lines."""
    ports = ports_for_sid(sid)
    if not ports:
        raise click.UsageError(f"No USB serial port found for J-Link SID {sid!r}")

    for port in ports:
        try:
            with serial.Serial(port, baudrate=115200, timeout=0.2) as ser:
                buf = b""
                deadline = time.time() + probe_timeout
                while time.time() < deadline:
                    chunk = ser.read(max(ser.in_waiting, 1))
                    if not chunk:
                        time.sleep(0.05)
                        continue
                    buf += chunk
                    if b"rssi_dB" in buf or (b"{" in buf and b"}" in buf):
                        console.print(f"[dim]Using {port} (nrfscan JSON)[/]")
                        return port
        except serial.SerialException:
            continue

    raise click.UsageError(
        f"No nrfscan JSON on SID {sid!r} within {probe_timeout}s "
        f"(tried {', '.join(ports)}). Reflash: make build-54l15 && make flash"
    )


def resolve_com(
    com: Optional[str],
    sid: Optional[str],
    autodetect: bool,
) -> str:
    if com:
        return _cu_device(com)
    if not sid:
        raise click.UsageError("Provide --com or --sid (J-Link serial number)")
    if autodetect:
        return autodetect_nrfscan_port(sid)
    ports = ports_for_sid(sid)
    if not ports:
        raise click.UsageError(f"No USB serial port found for J-Link SID {sid!r}")
    if len(ports) > 1:
        console.print(
            f"[yellow]SID {sid} has {len(ports)} VCOM interfaces: "
            f"{', '.join(ports)}[/]"
        )
        console.print(
            "[yellow]Using first; pass --com explicitly or use --autodetect[/]"
        )
    return ports[0]


class ScanReport:
    def __init__(self, obj: Optional[dict[str, Any]] = None) -> None:
        self.obj: dict[str, Any] = obj or {}
        self.filename: Optional[str] = None
        self.rssi_db: np.ndarray = np.array([], dtype=int)
        self.duration_us: int = -1
        self.channels: int = RSSI_CHANNELS_2M
        self.freq_base_mhz: int = RSSI_FREQ_BASE_MHZ
        self.freq_step_mhz: int = RSSI_FREQ_STEP_2M
        self.scan_duration_us: int = -1
        self.interval_ms: int = -1
        self.scan_count: int = -1
        self.settle_us: int = -1
        self.disable_period_ch: int = -1
        self.radio_2mbit: int = -1
        self.time_hfclk_us: int = -1
        self.time_ready_us: int = -1
        self.time_disable_us: int = -1
        self.time_settle_us: int = -1
        self.time_rssi_us: int = -1
        self.time_final_disable_us: int = -1
        self.time_other_us: int = -1
        self.count_ready: int = -1
        self.count_disable: int = -1
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
            if not raw:
                extra = ser.read(max(ser.in_waiting, 1))
                if extra:
                    raise ValueError(
                        f"no full line within {timeout}s; got {len(extra)} byte(s): {extra!r}"
                    )
                raise ValueError(f"no data within {timeout}s on {port}")
        self.obj = parse_json_line(raw)

    def save(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as fo:
            json.dump(self.obj, fo)

    def load(self) -> None:
        self.rssi_db = np.array(self.obj["rssi_dB"], dtype=int)
        if "scan_duration_us" in self.obj:
            self.scan_duration_us = int(self.obj["scan_duration_us"])
        else:
            self.scan_duration_us = int(self.obj.get("duration_us", -1))
        self.duration_us = self.scan_duration_us
        self.radio_2mbit = int(self.obj.get("radio_2mbit", -1))
        self.channels = int(self.obj.get("channels", len(self.rssi_db)))
        self.freq_base_mhz = int(self.obj.get("freq_base_mhz", RSSI_FREQ_BASE_MHZ))
        if "freq_step_mhz" in self.obj:
            self.freq_step_mhz = int(self.obj["freq_step_mhz"])
        elif self.radio_2mbit == 1:
            self.freq_step_mhz = RSSI_FREQ_STEP_2M
        elif self.radio_2mbit == 0:
            self.freq_step_mhz = RSSI_FREQ_STEP_1M
        elif self.channels >= RSSI_CHANNELS_1M:
            self.freq_step_mhz = RSSI_FREQ_STEP_1M
        else:
            self.freq_step_mhz = RSSI_FREQ_STEP_2M
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
        self.settle_us = int(self.obj.get("settle_us", -1))
        self.disable_period_ch = int(self.obj.get("disable_period_ch", -1))
        self.time_hfclk_us = int(self.obj.get("time_hfclk_us", -1))
        self.time_ready_us = int(self.obj.get("time_ready_us", -1))
        self.time_disable_us = int(self.obj.get("time_disable_us", -1))
        self.time_settle_us = int(self.obj.get("time_settle_us", -1))
        self.time_rssi_us = int(self.obj.get("time_rssi_us", -1))
        self.time_final_disable_us = int(self.obj.get("time_final_disable_us", -1))
        self.time_other_us = int(self.obj.get("time_other_us", -1))
        self.count_ready = int(self.obj.get("count_ready", -1))
        self.count_disable = int(self.obj.get("count_disable", -1))
        self.sleep_sec = int(self.obj.get("sleep_sec", -1))
        self.wake_count = int(self.obj.get("wake_count", -1))

    @property
    def mhz(self) -> np.ndarray:
        n = len(self.rssi_db)
        return np.array(
            [self.freq_base_mhz + ch * self.freq_step_mhz for ch in range(n)],
            dtype=float,
        )

    def peak(self) -> tuple[int, int, float]:
        """Return (channel, rssi_dBm, mhz) of strongest reading."""
        ch = int(np.argmax(self.rssi_db))
        return ch, int(self.rssi_db[ch]), float(self.mhz[ch])

    def _display_cols_per_bin(self, n: Optional[int] = None) -> int:
        n = len(self.rssi_db) if n is None else n
        return max(1, DISPLAY_COLUMNS // n) if n else 1

    def expand_rssi_for_display(self, rssi: np.ndarray) -> np.ndarray:
        """Repeat each bin so the chart spans DISPLAY_COLUMNS (same width as 1 MHz / 80)."""
        n = len(rssi)
        if n == 0:
            return np.array([], dtype=int)
        k = self._display_cols_per_bin(n)
        return np.repeat(rssi, k)[:DISPLAY_COLUMNS]

    def mhz_at_display_col(self, col: int) -> float:
        """1 MHz column index on the fixed-width axis (2400 + col for default span)."""
        if DISPLAY_COLUMNS <= 1:
            return float(self.freq_base_mhz)
        end_mhz = self.freq_base_mhz + DISPLAY_COLUMNS - 1
        return self.freq_base_mhz + col * (end_mhz - self.freq_base_mhz) / (DISPLAY_COLUMNS - 1)

    def display_mhz_axis(self) -> np.ndarray:
        return np.array([self.mhz_at_display_col(c) for c in range(DISPLAY_COLUMNS)], dtype=float)

    @staticmethod
    def _rssi_bar_level(dbm: float, height: int) -> int:
        """Map dBm to bar row using fixed axis [_BAR_FLOOR_DBM, _BAR_CEIL_DBM]."""
        clamped = max(_BAR_FLOOR_DBM, min(_BAR_CEIL_DBM, dbm))
        span = _BAR_CEIL_DBM - _BAR_FLOOR_DBM
        if height <= 1:
            return 0
        return int(round((clamped - _BAR_FLOOR_DBM) / span * (height - 1)))

    def print_bar_chart(
        self,
        height: int = _BAR_HEIGHT_DEFAULT,
        max_hold: Optional[MaxHold] = None,
        new_peaks: int = 0,
    ) -> None:
        """Vertical bar chart; optional ▲ marks session max per frequency."""
        if len(self.rssi_db) == 0:
            return

        rssi = self.expand_rssi_for_display(self.rssi_db)
        n = len(rssi)
        peak_col = int(np.argmax(rssi))
        peak_bin = min(peak_col // self._display_cols_per_bin(), len(self.rssi_db) - 1)
        height = max(4, height)
        span = _BAR_CEIL_DBM - _BAR_FLOOR_DBM
        hold_rssi = None
        if max_hold is not None:
            hold_rssi = self.expand_rssi_for_display(max_hold.rssi)

        title = (
            f"[bold]RSSI spectrum[/]  scan={self.scan_count}  "
            f"[dim]{int(self.mhz[0])}–{int(self.mhz[-1])} MHz  "
            f"{self.scan_duration_us} us  axis {_BAR_CEIL_DBM}..{_BAR_FLOOR_DBM} dBm[/]"
        )
        if max_hold is not None:
            title += "  [dim]█ now  ▲ session max[/]"
        console.print(title)

        for row in range(height - 1, -1, -1):
            tick_dbm = _BAR_FLOOR_DBM + (row / (height - 1)) * span if height > 1 else _BAR_FLOOR_DBM
            line = Text(f"{tick_dbm:4.0f} │")
            for i in range(n):
                v = float(rssi[i])
                cur_level = self._rssi_bar_level(v, height)
                max_level = -1
                is_session_max = False
                if hold_rssi is not None and hold_rssi[i] > _RSSI_MIN:
                    max_level = self._rssi_bar_level(float(hold_rssi[i]), height)
                    is_session_max = v >= hold_rssi[i]

                if cur_level >= row:
                    if is_session_max and row == max_level:
                        line.append("█", style="bold yellow")
                    elif i == peak_col:
                        line.append("█", style="bold green")
                    elif v >= -35:
                        line.append("█", style="bold green")
                    elif v >= -50:
                        line.append("█", style="green")
                    else:
                        line.append("█", style="cyan")
                elif hold_rssi is not None and max_level == row:
                    line.append("▲", style="bold magenta")
                else:
                    line.append(" ")
            console.print(line, highlight=False)

        axis = Text("     └" + "─" * n)
        console.print(axis, highlight=False)

        label_row = Text("      ")
        tick_cols = [0, n // 4, n // 2, 3 * n // 4, n - 1]
        ticks_mhz = [int(self.mhz_at_display_col(c)) for c in tick_cols]
        pos = 0
        for col, mhz_val in zip(tick_cols, ticks_mhz):
            label = str(mhz_val)
            if col > pos:
                label_row.append(" " * (col - pos))
                pos = col
            label_row.append(label, style="dim")
            pos += len(label)
        console.print(label_row, highlight=False)

        footer = (
            f"      [dim]now peak {int(self.mhz[peak_bin])} MHz  {int(self.rssi_db[peak_bin])} dBm[/]"
        )
        if max_hold is not None:
            session_peak_bin = int(np.argmax(max_hold.rssi))
            footer += (
                f"  [magenta]▲ max {int(self.mhz[session_peak_bin])} MHz  "
                f"{int(max_hold.rssi[session_peak_bin])} dBm[/]"
            )
            if new_peaks:
                footer += f"  [dim]({new_peaks} bins new max)[/]"
        console.print(footer, highlight=False)

    def print_summary(
        self,
        show_bars: bool = True,
        bar_height: int = _BAR_HEIGHT_DEFAULT,
        max_hold: Optional[MaxHold] = None,
        new_peaks: int = 0,
    ) -> None:
        ch, rssi, mhz = self.peak()
        table = Table(title="nrfscan report", show_header=True)
        table.add_column("field", style="cyan")
        table.add_column("value", justify="right")
        table.add_row("scan_count", str(self.scan_count))
        table.add_row("channels", str(self.channels))
        table.add_row("scan_duration_us", str(self.scan_duration_us))
        table.add_row("interval_ms", str(self.interval_ms))
        table.add_row("settle_us", str(self.settle_us))
        table.add_row("disable_period_ch", str(self.disable_period_ch))
        table.add_row("radio_2mbit", str(self.radio_2mbit))
        if self.time_hfclk_us >= 0:
            table.add_row("time_hfclk_us", str(self.time_hfclk_us))
            table.add_row("time_ready_us", str(self.time_ready_us))
            table.add_row("time_disable_us", str(self.time_disable_us))
            table.add_row("time_settle_us", str(self.time_settle_us))
            table.add_row("time_rssi_us", str(self.time_rssi_us))
            table.add_row("time_final_disable_us", str(self.time_final_disable_us))
            table.add_row("time_other_us", str(self.time_other_us))
            table.add_row("count_ready", str(self.count_ready))
            table.add_row("count_disable", str(self.count_disable))
        table.add_row("peak channel", str(ch))
        table.add_row("peak MHz", f"{mhz:.0f}")
        table.add_row("peak rssi_dB", str(rssi))
        table.add_row("rssi min", str(int(self.rssi_db.min())))
        table.add_row("rssi max", str(int(self.rssi_db.max())))
        console.print(table)
        if show_bars:
            console.print()
            self.print_bar_chart(height=bar_height, max_hold=max_hold, new_peaks=new_peaks)

    def plot(self, title: Optional[str] = None) -> None:
        mhz = self.display_mhz_axis()
        rssi = self.expand_rssi_for_display(self.rssi_db)
        fig, ax = plt.subplots(figsize=(12, 4))
        ax.plot(mhz, rssi, color="steelblue", linewidth=1.2)
        ax.scatter(self.mhz, self.rssi_db, s=20, color="steelblue", zorder=3)
        for adv_mhz in (2402, 2426, 2480):
            ax.axvline(adv_mhz, color="orange", alpha=0.35, linestyle="--")
        ax.set_xlim(self.freq_base_mhz, self.freq_base_mhz + DISPLAY_COLUMNS - 1)
        ax.set_xlabel("Frequency [MHz]")
        ax.set_ylabel("RSSI [dBm]")
        ax.set_title(title or f"scan={self.scan_count}  {self.scan_duration_us} us")
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        plt.show()


class MaxHold:
    """Running maximum RSSI per frequency bin (e.g. during watch)."""

    def __init__(self, n: int = RSSI_CHANNELS_2M) -> None:
        self.rssi = np.full(n, _RSSI_MIN, dtype=int)
        self.scan_at_max = np.full(n, -1, dtype=int)

    def update(self, report: ScanReport) -> int:
        """Merge report; return number of bins that set a new session maximum."""
        n = min(len(self.rssi), len(report.rssi_db))
        new_peaks = 0
        for i in range(n):
            v = int(report.rssi_db[i])
            if v > self.rssi[i]:
                self.rssi[i] = v
                self.scan_at_max[i] = report.scan_count
                new_peaks += 1
        return new_peaks

    @classmethod
    def from_reports(cls, reports: list[ScanReport]) -> MaxHold:
        hold = cls()
        for r in reports:
            hold.update(r)
        return hold


@click.group()
def cli() -> None:
    """Read and plot nrfscan UART JSON logs."""


@cli.command("list-ports")
@click.option(
    "--sid",
    default=None,
    help="J-Link serial number (e.g. 001057706325); omit to list all ports",
)
def list_ports_cmd(sid: Optional[str]) -> None:
    """List serial ports (optionally filtered by J-Link SID)."""
    table = Table("device", "SID", "description")
    for info in serial.tools.list_ports.comports():
        if sid and info.serial_number != sid and sid not in (info.device or ""):
            continue
        table.add_row(_cu_device(info.device), info.serial_number or "", info.description or "")
    console.print(table)


@cli.command()
@click.option("--com", default=None, help="Serial port (e.g. /dev/cu.usbmodem…)")
@click.option("--sid", default=None, help="J-Link SID (e.g. 001057706325)")
@click.option(
    "--autodetect",
    is_flag=True,
    help="With --sid: pick the VCOM that sends nrfscan JSON",
)
@click.option("--timeout", default=30.0, show_default=True, help="Seconds to wait for next report")
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
@click.option("--track-max", is_flag=True, help="Show session-max markers (single scan)")
def read(
    com: Optional[str],
    sid: Optional[str],
    autodetect: bool,
    timeout: float,
    no_bars: bool,
    bar_height: int,
    track_max: bool,
) -> None:
    """Read one report from the device and print a summary."""
    com = resolve_com(com, sid, autodetect)
    r = ScanReport.from_com(com, timeout=timeout)
    hold = None
    if track_max:
        hold = MaxHold()
        hold.update(r)
    r.print_summary(
        show_bars=not no_bars,
        bar_height=bar_height,
        max_hold=hold,
    )


@cli.command()
@click.option("--com", default=None, help="Serial port")
@click.option("--sid", default=None, help="J-Link SID (e.g. 001057706325)")
@click.option(
    "--autodetect",
    is_flag=True,
    help="With --sid: pick the VCOM that sends nrfscan JSON",
)
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
@click.option("--no-track-max", is_flag=True, help="Do not show per-frequency session max markers")
def watch(
    com: Optional[str],
    sid: Optional[str],
    autodetect: bool,
    save_dir: Optional[str],
    count: int,
    timeout: float,
    do_plot: bool,
    no_bars: bool,
    bar_height: int,
    no_track_max: bool,
) -> None:
    """Stream reports as the device wakes and transmits."""
    com = resolve_com(com, sid, autodetect or bool(sid))
    if save_dir and not os.path.isdir(save_dir):
        os.makedirs(save_dir)

    n = 0
    max_hold = MaxHold() if not no_track_max else None
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
            new_peaks = max_hold.update(r) if max_hold is not None else 0
            console.rule(f"report {n}")
            r.print_summary(
                show_bars=not no_bars,
                bar_height=bar_height,
                max_hold=max_hold,
                new_peaks=new_peaks,
            )

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
@click.option("--sid", default=None, help="J-Link SID (e.g. 001057706325)")
@click.option("--autodetect", is_flag=True, help="With --sid: pick VCOM with nrfscan JSON")
@click.option("--timeout", default=30.0, show_default=True)
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
@click.option("--gui", is_flag=True, help="Open matplotlib window")
def plot(
    filename: Optional[str],
    com: Optional[str],
    sid: Optional[str],
    autodetect: bool,
    timeout: float,
    no_bars: bool,
    bar_height: int,
    gui: bool,
) -> None:
    """Show RSSI spectrum from a file or one live reading."""
    if com or sid:
        com = resolve_com(com, sid, autodetect or bool(sid))
        r = ScanReport.from_com(com, timeout=timeout)
    elif filename:
        r = ScanReport.from_file(filename)
    else:
        raise click.UsageError("Provide --file or --com / --sid")
    r.print_summary(show_bars=not no_bars, bar_height=bar_height)
    if gui:
        r.plot(title=os.path.basename(filename) if filename else None)


@cli.command("plot-dir")
@click.argument("dirname", type=click.Path(exists=True, file_okay=False))
@click.option("--latest", is_flag=True, help="Only show the newest file")
@click.option("--no-bars", is_flag=True, help="Skip terminal bar chart")
@click.option("--bar-height", default=_BAR_HEIGHT_DEFAULT, show_default=True, help="Bar chart rows")
@click.option("--gui", is_flag=True, help="Open matplotlib window")
@click.option("--track-max", is_flag=True, help="Overlay max-hold from all files in directory")
def plot_dir(dirname: str, latest: bool, no_bars: bool, bar_height: int, gui: bool,
             track_max: bool) -> None:
    """Show saved JSON spectrum reports from a directory."""
    files = sorted(glob.glob(os.path.join(dirname, "*.json")))
    if not files:
        console.print(f"[yellow]no .json files in {dirname}[/]")
        return
    if latest:
        files = [files[-1]]

    max_hold = None
    if track_max and not latest:
        reports = [ScanReport.from_file(p) for p in files]
        max_hold = MaxHold.from_reports(reports)
    elif track_max and latest:
        max_hold = MaxHold()
        max_hold.update(ScanReport.from_file(files[0]))

    for path in files:
        r = ScanReport.from_file(path)
        console.rule(os.path.basename(path))
        r.print_summary(show_bars=not no_bars, bar_height=bar_height, max_hold=max_hold)
        if gui:
            r.plot(title=os.path.basename(path))


if __name__ == "__main__":
    cli()
