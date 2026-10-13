"""Measure an already-running Forge benchmark over CDC; never flash or reset.

Run with an unused, new report directory. Optional --uac opens the FM1 FW
Factory WASAPI input/output together during the final CPU benchmark run.
Dependencies: pyserial; with --uac, numpy and sounddevice. Device timer samples
are quantized to 500 us and include interrupt work during active slices. They
are not an independent PLL measurement or a device CPU utilization estimate.
"""
from __future__ import annotations

import argparse
from contextlib import nullcontext
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import time


IDENTITY = "FM1DIAG/1 FM1-FORGE/1 UBOOT=SERIAL COMMIT=BLOCKED"
TOTAL = 16777216
EXPECTED = {"x": 0xDB418D78, "acc": 0xD1593F35, "checksum": 0x219BCD44}
COMMAND_ROWS = {
    "FORGE CLOCK": ("FORGE CLOCK ",),
    "FORGE STATUS": ("FORGE STATUS ",),
    "FORGE AUDIO": ("FORGE AUDIO ", "USB AUDIO ", "USB TRANSPORT "),
    "FORGE INPUT": ("FORGE INPUT ",) + tuple(f"FORGE KNOB id={i} " for i in range(7)),
    "FORGE LCD": ("FORGE LCD ",),
}


def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"\b([a-z_][a-z_0-9]*)=([^\s]+)", line))


def integer(row: dict[str, str], key: str) -> int:
    try:
        return int(row[key], 10)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"Missing or invalid decimal telemetry field {key}") from exc


def parse_benchmark(lines: list[str]) -> dict:
    state_rows = [fields(line) for line in lines if line.startswith("FORGE BENCH state=")]
    checksum_rows = [fields(line) for line in lines if line.startswith("FORGE BENCH x=")]
    if len(state_rows) != 1 or len(checksum_rows) != 1:
        raise ValueError("Expected exactly one benchmark state row and checksum row")
    result = dict(state_rows[0])
    for key in ("done", "total", "active_ticks", "wall_ticks", "batches", "tick_us", "queued", "cancel", "fault"):
        result[key] = integer(result, key)
    for key in (*EXPECTED, "expected"):
        try:
            result[key] = int(checksum_rows[0][key], 16)
        except (KeyError, ValueError) as exc:
            raise ValueError(f"Missing or invalid hexadecimal benchmark field {key}") from exc
    if result["total"] != TOTAL or result["tick_us"] != 500 or result["expected"] != EXPECTED["checksum"]:
        raise ValueError("Device benchmark contract does not match this runner")
    if result["fault"] or result["state"] == "fault":
        raise ValueError(f"Device benchmark fault: {result}")
    if result["state"] not in ("idle", "queued", "running", "complete", "cancelled"):
        raise ValueError("Unknown benchmark state")
    if result["state"] == "complete":
        if result["done"] != TOTAL or any(result[key] != value for key, value in EXPECTED.items()):
            raise ValueError("Completed benchmark failed iteration/checksum validation")
        if result["queued"] or result["cancel"] or result["active_ticks"] <= 0 or result["wall_ticks"] < result["active_ticks"]:
            raise ValueError("Invalid completed benchmark timing or request state")
    result["active_seconds"] = result["active_ticks"] * 0.0005
    result["wall_seconds"] = result["wall_ticks"] * 0.0005
    result["active_quantization_bound_seconds"] = result["batches"] * 0.0005
    return result


class SerialSession:
    def __init__(self, port, transcript):
        self.port = port
        self.transcript = transcript
        self.origin = time.monotonic()

    def log(self, direction: str, text: str):
        self.transcript.write(f"{time.monotonic() - self.origin:.6f} {direction} {text}\n")
        self.transcript.flush()

    def exchange(self, command: str, prefixes: tuple[str, ...], timeout: float = 3.0) -> list[str]:
        self.log(">", command)
        self.port.write((command + "\n").encode("ascii"))
        self.port.flush()
        found = {}
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", "replace").strip()
            self.log("<", line)
            if line.startswith(("ERROR ", "ERR ")):
                raise RuntimeError(f"{command}: {line}")
            if command == "HELLO" and line.startswith("FM1DIAG/1 ") and line != IDENTITY:
                raise RuntimeError(f"Wrong device firmware identity: {line}")
            for prefix in prefixes:
                if line.startswith(prefix):
                    found[prefix] = line
            if len(found) == len(prefixes):
                return [found[prefix] for prefix in prefixes]
        raise TimeoutError(f"Timed out awaiting {command}; received {list(found)}")

    def identify(self):
        # Only the read-only HELLO probe is retried: CDC's first command can be
        # lost while opening. START is never retried after an uncertain reply.
        for attempt in range(3):
            try:
                answer = self.exchange("HELLO", ("FM1DIAG/1 ",), timeout=1.5)
                if answer != [IDENTITY]:
                    raise RuntimeError("Forge HELLO identity mismatch")
                time.sleep(0.1)
                return answer[0]
            except TimeoutError:
                if attempt == 2:
                    raise

    def bench(self, action: str = "STATUS", timeout: float = 3.0) -> dict:
        return parse_benchmark(self.exchange(f"FORGE BENCH {action}", ("FORGE BENCH state=", "FORGE BENCH x="), timeout))

    def query(self, command: str, prefixes: tuple[str, ...]) -> list[str]:
        # These queries are read-only. A bounded retry tolerates CDC wake-up
        # loss without ever retrying a state-changing benchmark START.
        try:
            return self.exchange(command, prefixes)
        except TimeoutError:
            return self.exchange(command, prefixes)

    def snapshot(self) -> dict:
        rows = {}
        for command, prefixes in COMMAND_ROWS.items():
            for line in self.query(command, prefixes):
                label = line.split(" ", 2)[:2]
                key = " ".join(label)
                if key == "FORGE KNOB":
                    key += " " + fields(line)["id"]
                rows[key] = fields(line)
        for name in ("FORGE CLOCK", "FORGE STATUS", "FORGE AUDIO", "FORGE LCD"):
            if integer(rows[name], "fault"):
                raise RuntimeError(f"Device peripheral fault in {name}: {rows[name]}")
        if integer(rows["FORGE STATUS"], "ready") != 1 or integer(rows["FORGE STATUS"], "running") != 1:
            raise RuntimeError("Forge peripherals are not ready/running")
        return rows


def counter_deltas(before: dict, after: dict) -> dict:
    counters = {
        "FORGE STATUS": ("frames", "audio_frames", "scans", "input_errors", "adc_errors"),
        "FORGE AUDIO": ("frames", "irqs"),
        "FORGE INPUT": ("scans", "errors", "adc_samples", "adc_errors"),
        "FORGE LCD": ("completed", "submitted", "busy", "errors"),
        "USB AUDIO": ("rx", "tx", "bad"),
        "USB TRANSPORT": ("submitted", "short", "busy", "starts", "stops"),
    }
    result = {}
    for row, names in counters.items():
        result[row] = {name: (integer(after[row], name) - integer(before[row], name)) & 0xFFFFFFFF for name in names}
    for key in ("under", "over"):
        old = [int(value) for value in before["USB AUDIO"][key].split(",")]
        new = [int(value) for value in after["USB AUDIO"][key].split(",")]
        if len(old) != 2 or len(new) != 2:
            raise ValueError(f"Invalid USB AUDIO {key} counter pair")
        result["USB AUDIO"][key] = [(new[i] - old[i]) & 0xFFFFFFFF for i in range(2)]
    return result


def select_serial_port(requested: str | None, ports) -> str:
    matches = [port.device for port in ports if port.vid == 0x3654 and port.pid == 0x5155]
    if requested:
        if requested.casefold() not in [name.casefold() for name in matches]:
            raise ValueError("Requested CDC port does not have FM1 VID3654/PID5155")
        return requested
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one FM1 CDC port; found {matches}")
    return matches[0]


def select_wasapi_devices(devices, hostapis) -> tuple[int, int]:
    wasapi = {index for index, api in enumerate(hostapis) if "wasapi" in api["name"].casefold()}
    inputs, outputs = [], []
    for index, device in enumerate(devices):
        if device["hostapi"] not in wasapi or "fm1 fw factory" not in device["name"].casefold():
            continue
        if device["max_input_channels"] >= 2:
            inputs.append(index)
        if device["max_output_channels"] >= 2:
            outputs.append(index)
    if len(inputs) != 1 or len(outputs) != 1:
        raise ValueError(f"Expected one FM1 FW Factory WASAPI input/output; found input={inputs}, output={outputs}")
    return inputs[0], outputs[0]


class UacExercise:
    """Bounded preallocated duplex stream; callback does no file/serial I/O."""
    def __init__(self):
        import numpy as np
        import sounddevice as sd
        self.np, self.sd = np, sd
        self.devices = sd.query_devices()
        self.pair = select_wasapi_devices(self.devices, sd.query_hostapis())
        sd.check_input_settings(device=self.pair[0], channels=2, dtype="float32", samplerate=48000)
        sd.check_output_settings(device=self.pair[1], channels=2, dtype="float32", samplerate=48000)
        phase = np.arange(48000, dtype=np.float64) / 48000
        self.tone = np.empty((48000, 2), dtype=np.float32)
        self.tone[:, 0] = np.sin(2 * np.pi * 440 * phase) * 0.005
        self.tone[:, 1] = np.sin(2 * np.pi * 660 * phase) * 0.005
        self.capture = np.empty((48000 * 75, 2), dtype=np.float32)
        self.position = self.captured = self.callbacks = self.capture_overflow_frames = 0
        self.flags = {key: 0 for key in ("input_underflow", "input_overflow", "output_underflow", "output_overflow", "priming_output")}
        self.stream = None
        self.started = None
        self.ended = None

    def callback(self, indata, outdata, frames, _time_info, status):
        self.callbacks += 1
        for key in self.flags:
            if getattr(status, key):
                self.flags[key] += 1
        offset = 0
        while offset < frames:
            count = min(frames - offset, len(self.tone) - self.position)
            outdata[offset:offset + count] = self.tone[self.position:self.position + count]
            offset += count
            self.position = (self.position + count) % len(self.tone)
        count = min(frames, len(self.capture) - self.captured)
        if count:
            self.capture[self.captured:self.captured + count] = indata[:count]
            self.captured += count
        self.capture_overflow_frames += frames - count

    def __enter__(self):
        self.stream = self.sd.Stream(device=self.pair, samplerate=48000, channels=2, dtype="float32", blocksize=0, latency="high", callback=self.callback)
        try:
            self.stream.start()
        except BaseException:
            self.stream.close()
            raise
        self.started = time.monotonic()
        time.sleep(2.0)
        return self

    def __exit__(self, _kind, _value, _traceback):
        try:
            self.stream.stop()
        finally:
            self.stream.close()
            self.ended = time.monotonic()

    def result(self):
        samples = self.capture[:self.captured]
        rms = self.np.sqrt(self.np.mean(self.np.square(samples, dtype=self.np.float64), axis=0)) if self.captured else (0.0, 0.0)
        return {"input_device": self.pair[0], "output_device": self.pair[1],
                "input_name": self.devices[self.pair[0]]["name"], "output_name": self.devices[self.pair[1]]["name"],
                "sample_rate": 48000, "channels": 2, "dtype": "float32", "output_peak": 0.005,
                "duration_seconds": (self.ended - self.started) if self.ended and self.started else None,
                "callbacks": self.callbacks, "captured_frames": self.captured,
                "capture_capacity_exceeded_frames": self.capture_overflow_frames,
                "host_callback_flags": self.flags, "capture_rms": [float(value) for value in rms],
                "capture_note": "Capture may be silent without a device key tone; host flags are distinct from device FIFO counters."}


def run_benchmark(session: SerialSession, number: int, uac: bool, result: dict | None = None) -> dict:
    exercise = UacExercise() if uac else None
    if result is None:
        result = {}
    result.update({"run": number, "uac_enabled": uac})
    with exercise if exercise else nullcontext():
        result["before"] = session.snapshot()
        state = session.bench()
        if state["state"] in ("queued", "running"):
            raise RuntimeError("A benchmark is already running; refusing to replace or cancel it")
        deadline = time.monotonic() + 60.0
        state = session.bench("START")
        while state["state"] in ("queued", "running"):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Device benchmark exceeded 60 seconds")
            time.sleep(min(0.5, remaining))
            state = session.bench(timeout=min(3.0, max(0.1, deadline - time.monotonic())))
        if state["state"] != "complete":
            raise RuntimeError(f"Benchmark did not complete: {state['state']}")
        result["benchmark"] = state
        result["after"] = session.snapshot()
        result["counter_deltas"] = counter_deltas(result["before"], result["after"])
    if exercise:
        result["uac"] = exercise.result()
        usb = result["counter_deltas"]["USB AUDIO"]
        if usb["rx"] <= 0 or usb["tx"] <= 0:
            raise RuntimeError("Duplex host stream did not advance both device RX/TX packet counters")
        if exercise.capture_overflow_frames:
            raise RuntimeError("Host capture exceeded its preallocated capacity")
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="FM1 CDC port; default requires one exact VID/PID match")
    parser.add_argument("--out", required=True, type=Path, help="New report directory; existing paths are refused")
    parser.add_argument("--runs", default=3, type=int)
    parser.add_argument("--uac", action="store_true", help="Exercise WASAPI full-duplex audio during the final run")
    args = parser.parse_args(argv)
    if not 1 <= args.runs <= 20:
        parser.error("--runs must be between 1 and 20")
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "ok": False, "runs": [],
              "measurement_note": "500 us device ticks; active slices include interrupts. No independent PLL or device CPU utilization measurement.",
              "commands_change_firmware": False, "uac_final_run": args.uac}
    session = None
    port = None
    owned_benchmark = False
    try:
        import serial
        import serial.tools.list_ports
        report["port"] = select_serial_port(args.port, serial.tools.list_ports.comports())
        with (args.out / "serial.log").open("w", encoding="utf-8") as transcript:
            port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
            port.port = report["port"]
            port.dtr, port.rts = True, False
            try:
                port.open()
                session = SerialSession(port, transcript)
                report["identity"] = session.identify()
                original = session.bench()
                if original["state"] in ("queued", "running"):
                    raise RuntimeError("A benchmark is already running; no commands will alter it")
                owned_benchmark = True
                for number in range(1, args.runs + 1):
                    run = {}
                    report["current_run"] = run
                    run_benchmark(session, number, args.uac and number == args.runs, run)
                    report["runs"].append(run)
                    del report["current_run"]
                    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                    print(f"Run {number}: active={run['benchmark']['active_seconds']:.4f}s wall={run['benchmark']['wall_seconds']:.4f}s checksum=219bcd44 uac={run['uac_enabled']}", flush=True)
                report["ok"] = True
            finally:
                if session and owned_benchmark:
                    try:
                        state = session.bench()
                        if state["state"] in ("queued", "running"):
                            report["cleanup_cancel"] = session.bench("CANCEL")
                    except Exception as exc:
                        report["cleanup_error"] = str(exc)
                if port:
                    port.close()
    except (Exception, KeyboardInterrupt) as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        print(report["error"], file=sys.stderr)
    finally:
        (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
