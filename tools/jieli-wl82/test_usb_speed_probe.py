import argparse
import ctypes as ct
import struct
import types
import unittest
from unittest.mock import patch

import usb_speed_probe as s


def connection(port=1, speed=1, status=1, vid=0x3654, pid=0x5155,
               bcd_usb=0x0200, pipes=0):
    descriptor = struct.pack("<BBHBBBBHHHBBBB", 18, 1, bcd_usb,
                             2, 2, 1, 64, vid, pid, 0x0200, 1, 2, 0, 1)
    return (struct.pack("<I", port) + descriptor +
            struct.pack("<BBBHII", 1, speed, 0, 7, pipes, status) +
            bytes(pipes * s.PIPE_INFO_SIZE))


def v2(flags=0, port=1, length=16):
    return struct.pack("<IIII", port, length, 7, flags)


class DecodeTests(unittest.TestCase):
    def test_sdk_packed_offsets_and_identity(self):
        raw = connection(pipes=2)
        self.assertEqual(len(raw), 35 + 2 * 11)
        parsed = s.parse_connection_ex(raw, len(raw), 1)
        self.assertEqual(parsed["raw_speed"], 1)
        self.assertEqual(parsed["device_address"], 7)
        self.assertEqual(parsed["open_pipes"], 2)
        self.assertEqual(parsed["configuration"], 1)
        self.assertEqual(parsed["device_descriptor"]["vid"], 0x3654)
        self.assertEqual(parsed["device_descriptor"]["pid"], 0x5155)

    def test_low_full_high_operating_speed(self):
        decoded_v2 = s.parse_connection_v2(v2(), 16, 1)
        for raw, name, rate in ((0, "low", 1.5), (1, "full", 12),
                                 (2, "high", 480)):
            with self.subTest(raw=raw):
                parsed = s.parse_connection_ex(connection(speed=raw), 35, 1)
                speed = s.decode_speed(parsed["raw_speed"], decoded_v2)
                self.assertEqual(speed["negotiated_speed"], name)
                self.assertEqual(speed["megabits_per_second"], rate)
                self.assertTrue(speed["speed_verified"])

    def test_usb_version_does_not_determine_speed(self):
        raw = connection(speed=1, bcd_usb=0x0300)
        parsed = s.parse_connection_ex(raw, 35, 1)
        self.assertEqual(s.decode_speed(parsed["raw_speed"], None)
                         ["negotiated_speed"], "full")

    def test_superspeed_operating_flags_overlay_ex_high(self):
        for flags, name in ((1, "super-speed-or-higher"),
                            (5, "super-speed-plus-or-higher")):
            speed = s.decode_speed(2, s.parse_connection_v2(v2(flags), 16, 1))
            self.assertEqual(speed["negotiated_speed"], name)
            self.assertIsNone(speed["megabits_per_second"])

    def test_capability_does_not_override_operating_speed(self):
        speed = s.decode_speed(2, s.parse_connection_v2(v2(2 | 8), 16, 1))
        self.assertEqual(speed["negotiated_speed"], "high")

    def test_missing_v2_does_not_falsely_confirm_high_speed(self):
        speed = s.decode_speed(2, None)
        self.assertFalse(speed["speed_verified"])
        self.assertIsNone(speed["megabits_per_second"])
        self.assertNotEqual(speed["negotiated_speed"], "high")

    def test_empty_port_descriptor_is_not_interpreted(self):
        raw = bytearray(35)
        struct.pack_into("<I", raw, 0, 1)
        self.assertNotIn("device_descriptor", s.parse_connection_ex(raw, 35, 1))
        # Live Windows hub drivers also zero ConnectionIndex on empty ports.
        parsed = s.parse_connection_ex(bytes(35), 35, 22)
        self.assertEqual(parsed["port"], 22)
        self.assertEqual(parsed["connection_status"], 0)
        self.assertNotIn("device_descriptor", parsed)

    def test_truncated_and_invalid_responses_are_rejected(self):
        raw = connection(pipes=1)
        for returned in (0, 34, 35, len(raw) - 1, len(raw) + 1):
            with self.subTest(returned=returned), self.assertRaises(ValueError):
                s.parse_connection_ex(raw, returned, 1)
        with self.assertRaises(ValueError):
            s.parse_connection_ex(raw, len(raw), 2)
        malformed = bytearray(connection())
        malformed[4] = 17
        with self.assertRaises(ValueError):
            s.parse_connection_ex(malformed, 35, 1)
        malformed = bytearray(connection())
        struct.pack_into("<I", malformed, 27, 0xffffffff)
        with self.assertRaises(ValueError):
            s.parse_connection_ex(malformed, 35, 1)

    def test_invalid_v2_and_inconsistent_speeds_are_rejected(self):
        for raw, returned in ((v2(), 15), (v2(), 17), (v2(port=2), 16),
                              (v2(length=12), 16)):
            with self.subTest(raw=raw, returned=returned):
                with self.assertRaises(ValueError):
                    s.parse_connection_v2(raw, returned, 1)
        with self.assertRaises(ValueError):
            s.decode_speed(1, s.parse_connection_v2(v2(1), 16, 1))
        with self.assertRaises(ValueError):
            s.decode_speed(99, None)


class Function:
    def __init__(self, call=lambda *args: 1):
        self.call = call

    def __call__(self, *args):
        return self.call(*args)


def libraries():
    kernel = types.SimpleNamespace(**{name: Function() for name in
        ("CreateFileW", "DeviceIoControl", "CloseHandle")})
    setup = types.SimpleNamespace(**{name: Function() for name in
        ("SetupDiGetClassDevsW", "SetupDiEnumDeviceInterfaces",
         "SetupDiGetDeviceInterfaceDetailW", "SetupDiDestroyDeviceInfoList")})
    return kernel, setup


class ApiTests(unittest.TestCase):
    def test_setupapi_enumerates_utf16_hub_path_with_abi_detail_size(self):
        kernel, setup = libraries()
        api = s.WindowsHubAPI(kernel, setup)
        path = r"\\?\usb#test-hub"
        encoded = (path + "\0").encode("utf-16-le")
        last_error, closed = [0], []
        setup.SetupDiGetClassDevsW.call = lambda *args: 0x100000001

        def enum(devices, device, guid, index, interface):
            self.assertEqual(devices, 0x100000001)
            self.assertIsNone(device)
            if index:
                last_error[0] = s.ERROR_NO_MORE_ITEMS
                return 0
            self.assertEqual(ct.cast(interface,
                ct.POINTER(s.SP_DEVICE_INTERFACE_DATA)).contents.cbSize,
                ct.sizeof(s.SP_DEVICE_INTERFACE_DATA))
            return 1

        def detail(devices, interface, buffer, size, required, device):
            ct.cast(required, ct.POINTER(s.U32)).contents.value = 4 + len(encoded)
            if buffer is None:
                last_error[0] = s.ERROR_INSUFFICIENT_BUFFER
                return 0
            self.assertEqual(s.U32.from_buffer(buffer).value,
                             8 if ct.sizeof(ct.c_void_p) == 8 else 6)
            ct.memmove(ct.addressof(buffer) + 4, encoded, len(encoded))
            return 1

        setup.SetupDiEnumDeviceInterfaces.call = enum
        setup.SetupDiGetDeviceInterfaceDetailW.call = detail
        setup.SetupDiDestroyDeviceInfoList.call = lambda handle: closed.append(handle) or 1
        with patch.object(s.ct, "get_last_error", side_effect=lambda: last_error[0],
                          create=True):
            self.assertEqual(api.hub_paths(), [path])
        self.assertEqual(closed, [0x100000001])

    def test_handles_and_pointer_arguments_use_pointer_width(self):
        kernel, setup = libraries()
        s.WindowsHubAPI(kernel, setup)
        self.assertIs(kernel.CreateFileW.restype, ct.c_void_p)
        self.assertIs(setup.SetupDiGetClassDevsW.restype, ct.c_void_p)
        self.assertIs(kernel.DeviceIoControl.argtypes[0], ct.c_void_p)
        self.assertIs(kernel.CloseHandle.argtypes[0], ct.c_void_p)
        self.assertIs(setup.SetupDiEnumDeviceInterfaces.argtypes[0], ct.c_void_p)
        self.assertEqual(ct.sizeof(s.SP_DEVICE_INTERFACE_DATA),
                         32 if ct.sizeof(ct.c_void_p) == 8 else 28)
        self.assertEqual(s.INVALID_HANDLE_VALUE,
                         (1 << (ct.sizeof(ct.c_void_p) * 8)) - 1)

    def test_hub_open_requests_no_write_access_and_preserves_handles(self):
        kernel, setup = libraries()
        calls = []
        kernel.CreateFileW.call = lambda *args: calls.append(args) or 0x100000001
        api = s.WindowsHubAPI(kernel, setup)
        self.assertEqual(api.open_hub("hub-path"), 0x100000001)
        self.assertEqual(calls[0], ("hub-path", 0, 3, None, 3, 0, None))

    def test_query_uses_same_in_out_buffer_and_returns_driver_count(self):
        kernel, setup = libraries()
        expected = connection()

        def driver(handle, code, source, source_size, dest, dest_size,
                   count, overlapped):
            self.assertEqual(handle, 0x100000001)
            self.assertEqual(code, s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX)
            self.assertIs(source, dest)
            self.assertEqual(source_size, dest_size)
            self.assertEqual(struct.unpack_from("<I", source.raw)[0], 1)
            self.assertIsNone(overlapped)
            ct.memmove(dest, expected, len(expected))
            ct.cast(count, ct.POINTER(s.U32)).contents.value = len(expected)
            return 1

        kernel.DeviceIoControl.call = driver
        api = s.WindowsHubAPI(kernel, setup)
        raw, count = api.query(0x100000001,
                              s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                              struct.pack("<I", 1) + bytes(383))
        self.assertEqual(count, 35)
        self.assertEqual(s.parse_connection_ex(raw, count, 1)["raw_speed"], 1)

    def test_ioctl_error_and_excessive_driver_count_are_rejected(self):
        kernel, setup = libraries()
        api = s.WindowsHubAPI(kernel, setup)
        kernel.DeviceIoControl.call = lambda *args: 0
        with patch.object(api, "_error", return_value=OSError("driver failure")):
            with self.assertRaisesRegex(OSError, "driver failure"):
                api.query(1, s.IOCTL_USB_GET_NODE_INFORMATION, bytes(76))

        def excessive(*args):
            ct.cast(args[6], ct.POINTER(s.U32)).contents.value = 77
            return 1

        kernel.DeviceIoControl.call = excessive
        with self.assertRaises(ValueError):
            api.query(1, s.IOCTL_USB_GET_NODE_INFORMATION, bytes(76))
        with self.assertRaisesRegex(ValueError, "read-only"):
            api.query(1, 0x220444, bytes(76))  # no reset/cycle/unknown IOCTLs


class FakeHubs:
    def __init__(self, speed=1, v2_error=False, connection_error=False):
        self.speed, self.v2_error = speed, v2_error
        self.connection_error = connection_error
        self.closed, self.calls = [], []

    def hub_paths(self):
        return ["test-hub"]

    def open_hub(self, path):
        return 0x100000001

    def close_hub(self, handle):
        self.closed.append(handle)

    def query(self, handle, code, initial):
        self.calls.append((code, initial))
        if code == s.IOCTL_USB_GET_NODE_INFORMATION:
            raw = bytearray(76)
            raw[6] = 3
            return bytes(raw), 76
        port = struct.unpack_from("<I", initial)[0]
        if code == s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
            if self.connection_error and port == 1:
                raise OSError("connection metadata failed")
            raw = connection(port=port, speed=self.speed,
                             vid=0x3654 if port == 1 else 0x1111,
                             status=0 if port == 3 else 1)
            return raw, len(raw)
        if code == s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
            self.assert_v2_input(initial)
            if self.v2_error:
                raise OSError("V2 unsupported")
            return v2(port=port), 16
        raise AssertionError("Unexpected non-GET hub operation")

    @staticmethod
    def assert_v2_input(initial):
        assert struct.unpack("<IIII", initial) == (1, 16, 4, 0)


class ProbeTests(unittest.TestCase):
    def test_filters_identity_and_queries_only_hub_metadata(self):
        api = FakeHubs(speed=2)
        report = s.probe(api=api)
        self.assertEqual(report["status"], "matched")
        self.assertEqual(report["connected_device_count"], 2)
        self.assertEqual(report["ports_queried"], 3)
        self.assertEqual(len(report["matches"]), 1)
        self.assertEqual(report["matches"][0]["megabits_per_second"], 480)
        self.assertEqual(api.closed, [0x100000001])
        self.assertTrue(all(code in (s.IOCTL_USB_GET_NODE_INFORMATION,
                                    s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                                    s.IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2)
                            for code, _ in api.calls))

    def test_v2_error_is_explicit_and_does_not_mislabel_high_speed(self):
        report = s.probe(api=FakeHubs(speed=2, v2_error=True))
        info = report["matches"][0]
        self.assertEqual(info["ex_v2_error"], "V2 unsupported")
        self.assertFalse(info["speed_verified"])
        self.assertNotEqual(info["negotiated_speed"], "high")

    def test_connection_failure_is_explicit_and_handle_closed(self):
        api = FakeHubs(connection_error=True)
        report = s.probe(api=api)
        self.assertEqual(report["status"], "not-found-with-errors")
        self.assertEqual(report["errors"][0]["port"], 1)
        self.assertIn("connection metadata failed", report["errors"][0]["error"])
        self.assertEqual(api.closed, [0x100000001])

    def test_cli_hex_identity_bounds(self):
        self.assertEqual(s.hex_id("0x3654"), 0x3654)
        self.assertEqual(s.hex_id("5155"), 0x5155)
        for text in ("10000", "-1", "invalid"):
            with self.assertRaises(argparse.ArgumentTypeError):
                s.hex_id(text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
