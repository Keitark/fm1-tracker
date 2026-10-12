"""Offline benchmark telemetry/selection regressions; no device handles."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("benchmark_forge_clock", Path(__file__).with_name("benchmark_forge_clock.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def complete(checksum="219bcd44", done=16777216):
    return [f"FORGE BENCH state=complete done={done} total=16777216 active_ticks=100 wall_ticks=150 batches=256 tick_us=500 queued=0 cancel=0 fault=0",
            f"FORGE BENCH x=db418d78 acc=d1593f35 checksum={checksum} expected=219bcd44"]


class ContractTests(unittest.TestCase):
    def test_complete_requires_full_work_and_exact_checksum(self):
        result = bench.parse_benchmark(complete())
        self.assertEqual(result["active_seconds"], .05)
        self.assertEqual(result["wall_seconds"], .075)
        self.assertEqual(result["active_quantization_bound_seconds"], .128)
        for rows in (complete(checksum="219bcd45"), complete(done=16777215), complete()[:1]):
            with self.assertRaises(ValueError):
                bench.parse_benchmark(rows)

    def test_rejects_device_fault_or_different_timer_contract(self):
        for old, new in (("fault=0", "fault=-82"), ("tick_us=500", "tick_us=1000"), ("expected=219bcd44", "expected=00000000")):
            with self.assertRaises(ValueError):
                bench.parse_benchmark([row.replace(old, new) for row in complete()])

    def test_exact_vid_pid_selection_refuses_ambiguity(self):
        ports = [SimpleNamespace(device="COM4", vid=0x3654, pid=0x5155)]
        self.assertEqual(bench.select_serial_port(None, ports), "COM4")
        self.assertEqual(bench.select_serial_port("com4", ports), "com4")
        with self.assertRaises(ValueError):
            bench.select_serial_port("COM5", ports)
        with self.assertRaises(ValueError):
            bench.select_serial_port(None, ports * 2)

    def test_wasapi_pair_excludes_other_apis_and_names(self):
        apis = [{"name": "MME"}, {"name": "Windows WASAPI"}]
        devices = [
            {"name": "FM1 FW Factory", "hostapi": 0, "max_input_channels": 2, "max_output_channels": 0},
            {"name": "Line (FM1 FW Factory)", "hostapi": 1, "max_input_channels": 2, "max_output_channels": 0},
            {"name": "Speakers (FM1 FW Factory)", "hostapi": 1, "max_input_channels": 0, "max_output_channels": 2},
        ]
        self.assertEqual(bench.select_wasapi_devices(devices, apis), (1, 2))
        with self.assertRaises(ValueError):
            bench.select_wasapi_devices(devices + [devices[1]], apis)

    def test_fifo_pairs_and_uint32_counter_wrap(self):
        row = {"FORGE STATUS": {name: "0" for name in ("frames", "audio_frames", "scans", "input_errors", "adc_errors")},
               "FORGE AUDIO": {"frames": "0", "irqs": "0"},
               "FORGE INPUT": {name: "0" for name in ("scans", "errors", "adc_samples", "adc_errors")},
               "FORGE LCD": {name: "0" for name in ("completed", "submitted", "busy", "errors")},
               "USB AUDIO": {"rx": "4294967294", "tx": "0", "bad": "0", "under": "3,4", "over": "5,6"},
               "USB TRANSPORT": {name: "0" for name in ("submitted", "short", "busy", "starts", "stops")}}
        after = {key: dict(value) for key, value in row.items()}
        after["USB AUDIO"].update(rx="2", under="3,5", over="7,6")
        delta = bench.counter_deltas(row, after)["USB AUDIO"]
        self.assertEqual(delta["rx"], 4)
        self.assertEqual(delta["under"], [0, 1])
        self.assertEqual(delta["over"], [2, 0])

    def test_fields_keeps_numbered_key_and_comma_values(self):
        self.assertEqual(bench.fields("USB AUDIO under=1,2 fps10=110"), {"under": "1,2", "fps10": "110"})


if __name__ == "__main__":
    unittest.main()
