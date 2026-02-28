#!/usr/bin/env python3
"""MeshCom serial monitor — tracks LoRa state machine, packet counts, and alerts.

Connects to a Heltec WiFi LoRa 32 V3 (or similar) via serial with DTR/RTS
disabled to avoid triggering a hardware reset. Logs all output to a timestamped
file in /tmp/meshcom_monitor/ and prints alerts + periodic summaries to console.

Usage:
    python3 tools/serial_monitor.py
    python3 tools/serial_monitor.py --port /dev/cu.usbserial-0001 --interval 300
"""

import argparse
import os
import re
import signal
import sys
import threading
import time
from collections import defaultdict
from datetime import datetime, timedelta

import serial


# ---------------------------------------------------------------------------
# Pattern definitions
# ---------------------------------------------------------------------------

RE_STATE_TRANSITION = re.compile(r"\[MC-SM\]\s+(\w+)\s*->\s*(\w+)\s+rc=(-?\d+)")
RE_BUFFER_DROP = re.compile(r"\[MC-DBG\]\s+(\w+_DROPPED)\s+buffer_full")
RE_CAD_SCAN = re.compile(r"\[MC-DBG\]\s+CAD_SCAN\s+result=(-?\d+)")
RE_CAD_GIVEUP = re.compile(r"\[MC-DBG\]\s+CAD_GIVEUP")
RE_RETRANSMIT_GIVEUP = re.compile(r"\[MC-DBG\]\s+RETRANSMIT_GIVEUP")
RE_WIFI_DBG = re.compile(r"\[WIFI-DBG\]\s+(.*)")
RE_UDP_RESET = re.compile(r"resetMeshComUDP")
RE_RING_STATUS = re.compile(
    r"\[MC-DBG\]\s+RING_STATUS\s+queued=(\d+)\s+pending=(\d+)\s+retrying=(\d+)\s+done=(\d+)"
)
RE_WATCHDOG = re.compile(r"\[MC-DBG\]\s+WATCHDOG_RECOVERY")

SIMPLE_COUNTERS = {
    "OnRxDone": "rx_packets",
    "OnRxTimeout": "rx_timeouts",
    "OnRxError": "rx_errors",
    "OnTXDone": "tx_packets",
    "OnTXTimeout": "tx_timeouts",
}

# Alert thresholds
STUCK_STATE_SECONDS = 30
NO_TRANSITION_SECONDS = 30
RX_TIMEOUT_ALERT_THRESHOLD = 10  # per summary interval
CAD_FALSE_POSITIVE_STREAK = 6
RX_RESTART_PER_MIN_THRESHOLD = 30
RADIO_SILENT_THRESHOLD = 30  # seconds without RX_TIMEOUT_FIRE = radio freeze
RING_ZOMBIE_CONSECUTIVE = 3  # consecutive RING_STATUS with retrying>0, queued==0

LORA_STATES = [
    "IDLE", "RX_LISTEN", "RX_PROCESS", "TX_PREPARE", "TX_ACTIVE", "TX_DONE",
]


class Monitor:
    def __init__(self, summary_interval: int) -> None:
        self.summary_interval = summary_interval
        self.start_time = time.monotonic()
        self.interval_start = time.monotonic()

        # Counters (reset each interval)
        self.counters: dict[str, int] = defaultdict(int)
        # Lifetime counters
        self.total: dict[str, int] = defaultdict(int)

        # State tracking
        self.current_state: str | None = None
        self.state_since = time.monotonic()
        self.last_transition = time.monotonic()
        self.state_time: dict[str, float] = defaultdict(float)  # per interval

        # CAD false-positive streak
        self.cad_streak = 0

        # RX restart tracking (per-minute ring)
        self.rx_restart_times: list[float] = []

        # WiFi/UDP status
        self.wifi_events_interval: list[str] = []
        self.udp_resets_interval = 0

        # Radio silent tracking (RX_TIMEOUT_FIRE gaps)
        self.last_rx_timeout_fire: float | None = None
        self.max_radio_gap: float = 0.0  # per interval
        self.max_radio_gap_total: float = 0.0  # lifetime
        self.radio_silent_events_interval: int = 0

        # Ring buffer zombie tracking
        self.ring_last_queued: int = 0
        self.ring_last_pending: int = 0
        self.ring_last_retrying: int = 0
        self.ring_last_done: int = 0
        self.ring_zombie_streak: int = 0  # consecutive reports with retrying>0, queued==0

        # Watchdog recovery tracking
        self.watchdog_recoveries_interval: int = 0

        # No-transition silence tracking (for resolved alerts)
        self.radio_was_silent = False

        # Alerts collected this interval
        self.alerts: list[str] = []

        self.lock = threading.Lock()

    # -- event processing ---------------------------------------------------

    def process_line(self, line: str) -> None:
        with self.lock:
            self._process(line)

    def _process(self, line: str) -> None:
        now = time.monotonic()

        # State machine transitions
        m = RE_STATE_TRANSITION.search(line)
        if m:
            from_st, to_st, rc = m.group(1), m.group(2), int(m.group(3))
            # Resolved: first transition after radio silence
            if self.radio_was_silent:
                silence_dur = now - self.last_transition
                self._resolved(
                    f"Radio alive after {silence_dur:.0f}s silence — "
                    f"woke up via {from_st}->{to_st}"
                )
                self.radio_was_silent = False
            # accumulate time in previous state
            if self.current_state:
                self.state_time[self.current_state] += now - self.state_since
            self.current_state = to_st
            self.state_since = now
            self.last_transition = now
            self.counters["transitions"] += 1
            self.total["transitions"] += 1
            if rc != 0:
                self._alert(f"State transition {from_st}->{to_st} returned rc={rc}")
            return

        # Simple counters
        for keyword, counter_name in SIMPLE_COUNTERS.items():
            if keyword in line:
                self.counters[counter_name] += 1
                self.total[counter_name] += 1
                if counter_name == "rx_errors":
                    self._alert(f"RX ERROR detected: {line.strip()}")
                if counter_name == "tx_timeouts":
                    self._alert(f"TX TIMEOUT: {line.strip()}")
                return

        # Buffer drops
        m = RE_BUFFER_DROP.search(line)
        if m:
            drop_type = m.group(1)
            self.counters[f"drop_{drop_type}"] += 1
            self.total[f"drop_{drop_type}"] += 1
            self._alert(f"BUFFER DROP: {drop_type}")
            return

        # CAD scan results
        m = RE_CAD_SCAN.search(line)
        if m:
            result = int(m.group(1))
            self.counters["cad_scans"] += 1
            self.total["cad_scans"] += 1
            if result == -702:
                self.cad_streak += 1
                self.counters["cad_false_pos"] += 1
                self.total["cad_false_pos"] += 1
                if self.cad_streak == CAD_FALSE_POSITIVE_STREAK:
                    self._alert(
                        f"CAD FALSE POSITIVE streak: {self.cad_streak}+ consecutive -702"
                    )
            else:
                self.cad_streak = 0
            return

        # CAD giveup
        if RE_CAD_GIVEUP.search(line):
            self.counters["cad_giveups"] += 1
            self.total["cad_giveups"] += 1
            self._alert("CAD GIVEUP — forced TX after max retries")
            return

        # Retransmit giveup
        if RE_RETRANSMIT_GIVEUP.search(line):
            self.counters["retransmit_fails"] += 1
            self.total["retransmit_fails"] += 1
            self._alert(f"RETRANSMIT GIVEUP: {line.strip()}")
            return

        # WiFi debug
        m = RE_WIFI_DBG.search(line)
        if m:
            detail = m.group(1).strip()
            self.wifi_events_interval.append(detail)
            self.counters["wifi_events"] += 1
            self.total["wifi_events"] += 1
            self._alert(f"WIFI EVENT: {detail}")
            return

        # UDP reset
        if RE_UDP_RESET.search(line):
            self.udp_resets_interval += 1
            self.counters["udp_resets"] += 1
            self.total["udp_resets"] += 1
            self._alert("UDP RESET detected")
            return

        # RING_STATUS parsing and zombie tracking
        m = RE_RING_STATUS.search(line)
        if m:
            self.ring_last_queued = int(m.group(1))
            self.ring_last_pending = int(m.group(2))
            self.ring_last_retrying = int(m.group(3))
            self.ring_last_done = int(m.group(4))
            if self.ring_last_retrying > 0 and self.ring_last_queued == 0:
                self.ring_zombie_streak += 1
                if self.ring_zombie_streak >= RING_ZOMBIE_CONSECUTIVE:
                    self._alert(
                        f"RING_ZOMBIE retrying={self.ring_last_retrying} "
                        f"stuck for {self.ring_zombie_streak * 30}s+"
                    )
            else:
                self.ring_zombie_streak = 0
            return

        # Watchdog recovery detection
        if RE_WATCHDOG.search(line):
            self.watchdog_recoveries_interval += 1
            self.counters["watchdog_recoveries"] += 1
            self.total["watchdog_recoveries"] += 1
            self._alert("WATCHDOG_RECOVERY — radio re-initialized after silence")
            return

        # RX restart (startReceive again) and RX_TIMEOUT_FIRE gap tracking
        if "startReceive again" in line or "RX_TIMEOUT_FIRE" in line:
            # Radio silent gap detection (only on RX_TIMEOUT_FIRE)
            if "RX_TIMEOUT_FIRE" in line:
                if self.last_rx_timeout_fire is not None:
                    gap = now - self.last_rx_timeout_fire
                    if gap > self.max_radio_gap:
                        self.max_radio_gap = gap
                    if gap > self.max_radio_gap_total:
                        self.max_radio_gap_total = gap
                    if gap > RADIO_SILENT_THRESHOLD:
                        self.radio_silent_events_interval += 1
                        self.total["radio_silent"] += 1
                        self._alert(f"RADIO_SILENT gap={gap:.0f}s (no RX_TIMEOUT_FIRE)")
                self.last_rx_timeout_fire = now

            prev_count = len(self.rx_restart_times)
            self.rx_restart_times.append(now)
            # prune older than 60s
            cutoff = now - 60
            self.rx_restart_times = [t for t in self.rx_restart_times if t > cutoff]
            cur_count = len(self.rx_restart_times)
            # alert once when crossing threshold, not on every event
            if (cur_count > RX_RESTART_PER_MIN_THRESHOLD
                    and prev_count <= RX_RESTART_PER_MIN_THRESHOLD):
                self._alert(
                    f"RX RESTART flood: {cur_count}/min"
                )

    def _alert(self, msg: str) -> None:
        ts = datetime.now().strftime("%H:%M:%S")
        alert_line = f"[ALERT {ts}] {msg}"
        self.alerts.append(alert_line)
        print(f"\033[91m{alert_line}\033[0m", flush=True)

    def _resolved(self, msg: str) -> None:
        ts = datetime.now().strftime("%H:%M:%S")
        resolved_line = f"[RESOLVED {ts}] {msg}"
        self.alerts.append(resolved_line)
        print(f"\033[92m{resolved_line}\033[0m", flush=True)

    # -- periodic checks (called from main thread) --------------------------

    def check_stuck_state(self) -> None:
        with self.lock:
            now = time.monotonic()
            if self.current_state and (now - self.state_since) > STUCK_STATE_SECONDS:
                if self.current_state not in ("RX_LISTEN",):
                    self._alert(
                        f"STUCK in {self.current_state} for "
                        f"{now - self.state_since:.0f}s"
                    )
            if (now - self.last_transition) > NO_TRANSITION_SECONDS:
                self.radio_was_silent = True
                self._alert(
                    f"No state transitions for {now - self.last_transition:.0f}s"
                )

    # -- summary ------------------------------------------------------------

    def print_summary(self) -> None:
        with self.lock:
            now = time.monotonic()
            elapsed = now - self.interval_start
            uptime = now - self.start_time

            # finalize state time for current state
            if self.current_state:
                self.state_time[self.current_state] += now - self.state_since
                self.state_since = now

            t_start = datetime.now() - timedelta(seconds=elapsed)
            t_end = datetime.now()
            h, rem = divmod(int(uptime), 3600)
            m, _ = divmod(rem, 60)

            # state distribution
            total_state_time = sum(self.state_time.values()) or 1.0
            state_pcts = {
                s: self.state_time.get(s, 0) / total_state_time * 100
                for s in LORA_STATES
                if self.state_time.get(s, 0) > 0
            }
            state_str = ", ".join(
                f"{s} {p:.1f}%" for s, p in sorted(state_pcts.items(), key=lambda x: -x[1])
            )

            # drop summary
            drops = sum(v for k, v in self.counters.items() if k.startswith("drop_"))

            # wifi/udp status
            wifi_str = "stable" if not self.wifi_events_interval else "EVENTS"
            udp_str = "ok" if self.udp_resets_interval == 0 else f"{self.udp_resets_interval} resets"

            alert_str = "none" if not self.alerts else f"{len(self.alerts)} (see above)"

            summary = (
                f"\n{'=' * 60}\n"
                f"SUMMARY {t_start:%H:%M}-{t_end:%H:%M} "
                f"(uptime: {h}h{m:02d}m)\n"
                f"{'=' * 60}\n"
                f"RX: {self.counters['rx_packets']} packets, "
                f"{self.counters['rx_errors']} errors, "
                f"{self.counters['rx_timeouts']} timeouts\n"
                f"TX: {self.counters['tx_packets']} packets, "
                f"{self.counters['tx_timeouts']} timeouts\n"
                f"State: {state_str or 'no transitions'}\n"
                f"Drops: {drops} | "
                f"CAD false pos: {self.counters.get('cad_false_pos', 0)} | "
                f"Retransmit fails: {self.counters.get('retransmit_fails', 0)}\n"
                f"Radio: {self.radio_silent_events_interval} silent events "
                f"(max gap: {self.max_radio_gap:.0f}s) | "
                f"Watchdog: {self.watchdog_recoveries_interval} recoveries\n"
                f"Ring:  queued={self.ring_last_queued} retrying={self.ring_last_retrying} "
                f"done={self.ring_last_done} (last seen)\n"
                f"WiFi: {wifi_str} | UDP: {udp_str}\n"
                f"Alerts: {alert_str}\n"
                f"{'=' * 60}\n"
            )
            # totals line
            summary += (
                f"  TOTALS: RX={self.total['rx_packets']} "
                f"TX={self.total['tx_packets']} "
                f"Errors={self.total['rx_errors']} "
                f"Drops={sum(v for k, v in self.total.items() if k.startswith('drop_'))} "
                f"RadioSilent={self.total['radio_silent']} "
                f"(max gap: {self.max_radio_gap_total:.0f}s) "
                f"Watchdog={self.total['watchdog_recoveries']}\n"
            )

            print(summary, flush=True)

            # reset interval counters
            self.counters = defaultdict(int)
            self.state_time = defaultdict(float)
            self.alerts = []
            self.wifi_events_interval = []
            self.udp_resets_interval = 0
            self.radio_silent_events_interval = 0
            self.max_radio_gap = 0.0
            self.watchdog_recoveries_interval = 0
            self.interval_start = now


def reader_thread(
    ser: serial.Serial,
    monitor: Monitor,
    log_file,
    stop_event: threading.Event,
) -> None:
    """Read serial lines, log to file, feed to monitor."""
    while not stop_event.is_set():
        try:
            raw = ser.readline()
        except serial.SerialException:
            if not stop_event.is_set():
                print("\033[91m[SERIAL] Connection lost, retrying...\033[0m", flush=True)
                time.sleep(2)
            continue
        if not raw:
            continue
        try:
            line = raw.decode("utf-8", errors="replace").rstrip()
        except Exception:
            continue
        if not line:
            continue

        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        log_line = f"{ts}  {line}\n"
        log_file.write(log_line)
        log_file.flush()

        monitor.process_line(line)


def main() -> None:
    parser = argparse.ArgumentParser(description="MeshCom serial monitor")
    parser.add_argument(
        "--port",
        default="/dev/cu.usbserial-0001",
        help="Serial port (default: /dev/cu.usbserial-0001)",
    )
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=300,
        help="Summary interval in seconds (default: 300)",
    )
    args = parser.parse_args()

    # Create log directory and file
    log_dir = "/tmp/meshcom_monitor"
    os.makedirs(log_dir, exist_ok=True)
    log_name = f"meshcom_{datetime.now():%Y-%m-%d_%H%M%S}.log"
    log_path = os.path.join(log_dir, log_name)

    print(f"MeshCom Serial Monitor")
    print(f"Port: {args.port} @ {args.baud}")
    print(f"Log:  {log_path}")
    print(f"Summary every {args.interval}s")
    print(f"Press Ctrl+C to stop\n")

    # Open serial with DTR/RTS disabled to avoid hardware reset
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 1
    ser.dtr = False
    ser.rts = False
    ser.open()

    monitor = Monitor(summary_interval=args.interval)
    stop_event = threading.Event()

    log_file = open(log_path, "w", encoding="utf-8")

    # Start reader thread
    reader = threading.Thread(
        target=reader_thread,
        args=(ser, monitor, log_file, stop_event),
        daemon=True,
    )
    reader.start()

    # Handle Ctrl+C
    def on_signal(sig, frame):
        stop_event.set()

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    # Main loop: periodic summaries + stuck-state checks
    try:
        while not stop_event.is_set():
            stop_event.wait(timeout=10)
            if stop_event.is_set():
                break
            monitor.check_stuck_state()

            elapsed = time.monotonic() - monitor.interval_start
            if elapsed >= args.interval:
                monitor.print_summary()
    finally:
        stop_event.set()
        reader.join(timeout=3)
        print("\n--- Final Summary ---")
        monitor.print_summary()
        log_file.close()
        ser.close()
        print(f"Log saved: {log_path}")


if __name__ == "__main__":
    main()
