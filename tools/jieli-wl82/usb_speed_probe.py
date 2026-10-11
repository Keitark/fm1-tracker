#!/usr/bin/env python3
"""Read negotiated USB speed from Windows hub-driver metadata.

This opens USB hub interfaces, never a device/serial interface. The three
GET_* IOCTLs query metadata already held by the Windows USB stack; they do not
send USB control transfers, reset a port, flash firmware, or change drivers.
Only ctypes from the Python standard library is required.

Definitions follow Microsoft's usbioctl.h/usbiodef.h (Windows SDK 26100) and
USBView: https://github.com/microsoft/windows-driver-samples/tree/main/usb/usbview
USB_NODE_CONNECTION_INFORMATION_EX is packed to one byte in usbioctl.h.
Its Speed field, rather than bcdUSB, reports the negotiated speed. EX_V2 is
also queried because EX alone reports SuperSpeed devices as UsbHighSpeed.
"""

from __future__ import annotations

import argparse
import ctypes as ct
import datetime as dt
import json
from pathlib import Path
import struct
import sys
import uuid


U8, U16, U32 = ct.c_uint8, ct.c_uint16, ct.c_uint32
HANDLE = ct.c_void_p
BOOL = ct.c_int32
INVALID_HANDLE_VALUE = ct.c_void_p(-1).value
ERROR_INSUFFICIENT_BUFFER = 122
ERROR_NO_MORE_ITEMS = 259
GUID_DEVINTERFACE_USB_HUB = "f18a0e88-c30c-11d0-8815-00a0c906bed8"
IOCTL_USB_GET_NODE_INFORMATION = (0x22 << 16) | (258 << 2)
IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX = (0x22 << 16) | (274 << 2)
IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 = (0x22 << 16) | (279 << 2)
CONNECTION_EX_SIZE = 35
PIPE_INFO_SIZE = 11
MAX_PIPES = 32
CONNECTION_V2_SIZE = 16
NODE_INFORMATION_SIZE = 76  # enum + packed 71-byte hub descriptor + BOOLEAN


class GUID(ct.Structure):
    _fields_ = [("Data1", U32), ("Data2", U16), ("Data3", U16),
                ("Data4", U8 * 8)]


class SP_DEVICE_INTERFACE_DATA(ct.Structure):
    _fields_ = [("cbSize", U32), ("InterfaceClassGuid", GUID),
                ("Flags", U32), ("Reserved", ct.c_size_t)]


def _checked_bytes(data: bytes, returned: int, minimum: int) -> bytes:
    if returned < minimum or returned > len(data):
        raise ValueError(f"Invalid returned byte count {returned}; "
                         f"expected {minimum}..{len(data)}")
    return data[:returned]


def parse_connection_ex(data: bytes, returned: int, expected_port: int) -> dict:
    """Decode the packed EX structure, rejecting partial driver responses."""
    data = _checked_bytes(data, returned, CONNECTION_EX_SIZE)
    port = struct.unpack_from("<I", data, 0)[0]
    pipes, status = struct.unpack_from("<II", data, 27)
    # Some Windows hub drivers return an entirely zeroed EX header for an
    # empty port, including the caller's ConnectionIndex. No device data or
    # speed may be interpreted from that response.
    zeroed_empty_port = port == 0 and status == 0 and not any(data[4:35])
    if port != expected_port and not zeroed_empty_port:
        raise ValueError(f"Returned port {port} differs from {expected_port}")
    if pipes > MAX_PIPES or len(data) < CONNECTION_EX_SIZE + pipes * PIPE_INFO_SIZE:
        raise ValueError(f"Incomplete or excessive pipe list ({pipes} pipes)")
    record = {"port": expected_port, "connection_status": status,
              "raw_speed": data[23], "device_is_hub": bool(data[24]),
              "device_address": struct.unpack_from("<H", data, 25)[0],
              "open_pipes": pipes, "configuration": data[22]}
    if status != 1:  # DeviceConnected; no valid descriptor for empty/error ports
        return record
    descriptor = struct.unpack_from("<BBHBBBBHHHBBBB", data, 4)
    if descriptor[:2] != (18, 1):
        raise ValueError("Connected device returned an invalid device descriptor")
    record["device_descriptor"] = dict(zip(
        ("length", "type", "bcd_usb", "device_class", "device_subclass",
         "device_protocol", "ep0_max_packet", "vid", "pid", "bcd_device",
         "manufacturer_string_index", "product_string_index",
         "serial_string_index", "configurations"), descriptor))
    return record


def parse_connection_v2(data: bytes, returned: int, expected_port: int) -> dict:
    data = _checked_bytes(data, returned, CONNECTION_V2_SIZE)
    port, length, protocols, flags = struct.unpack_from("<IIII", data)
    if port != expected_port or length != CONNECTION_V2_SIZE:
        raise ValueError("Invalid EX_V2 connection index or structure length")
    return {"supported_protocols": protocols, "flags": flags,
            "operating_superspeed": bool(flags & 1),
            "superspeed_capable": bool(flags & 2),
            "operating_superspeed_plus": bool(flags & 4),
            "superspeed_plus_capable": bool(flags & 8)}


def decode_speed(raw_speed: int, v2: dict | None) -> dict:
    """Capabilities alone never override the operating speed."""
    if raw_speed not in (0, 1, 2):
        raise ValueError(f"Unsupported EX speed value {raw_speed}")
    if v2 and (v2["operating_superspeed"] or v2["operating_superspeed_plus"]):
        if raw_speed != 2:
            raise ValueError("EX and EX_V2 negotiated-speed fields disagree")
        if v2["operating_superspeed_plus"]:
            return {"negotiated_speed": "super-speed-plus-or-higher",
                    "megabits_per_second": None, "speed_verified": True}
        return {"negotiated_speed": "super-speed-or-higher",
                "megabits_per_second": None, "speed_verified": True}
    if raw_speed == 2 and v2 is None:
        return {"negotiated_speed": "high-or-super-speed (EX_V2 unavailable)",
                "megabits_per_second": None, "speed_verified": False}
    names = (("low", 1.5), ("full", 12), ("high", 480))
    name, rate = names[raw_speed]
    return {"negotiated_speed": name, "megabits_per_second": rate,
            "speed_verified": True}


class WindowsHubAPI:
    """Pointer-width-safe SetupAPI enumeration and hub-only GET IOCTLs."""

    def __init__(self, kernel32=None, setupapi=None):
        if kernel32 is None:
            if sys.platform != "win32":
                raise OSError("This probe requires Windows USB hub drivers")
            kernel32 = ct.WinDLL("kernel32", use_last_error=True)
            setupapi = ct.WinDLL("setupapi", use_last_error=True)
        self.kernel32, self.setupapi = kernel32, setupapi
        signatures = (
            (setupapi.SetupDiGetClassDevsW, [ct.POINTER(GUID), ct.c_wchar_p,
                                           HANDLE, U32], HANDLE),
            (setupapi.SetupDiEnumDeviceInterfaces,
             [HANDLE, HANDLE, ct.POINTER(GUID), U32,
              ct.POINTER(SP_DEVICE_INTERFACE_DATA)], BOOL),
            (setupapi.SetupDiGetDeviceInterfaceDetailW,
             [HANDLE, ct.POINTER(SP_DEVICE_INTERFACE_DATA), ct.c_void_p,
              U32, ct.POINTER(U32), HANDLE], BOOL),
            (setupapi.SetupDiDestroyDeviceInfoList, [HANDLE], BOOL),
            (kernel32.CreateFileW, [ct.c_wchar_p, U32, U32, HANDLE,
                                   U32, U32, HANDLE], HANDLE),
            (kernel32.DeviceIoControl, [HANDLE, U32, ct.c_void_p, U32,
                                       ct.c_void_p, U32, ct.POINTER(U32),
                                       HANDLE], BOOL),
            (kernel32.CloseHandle, [HANDLE], BOOL),
        )
        for function, arguments, result in signatures:
            function.argtypes, function.restype = arguments, result

    @staticmethod
    def _error(action: str) -> OSError:
        code = ct.get_last_error()
        return OSError(code, f"{action}: {ct.FormatError(code).strip()}")

    def hub_paths(self) -> list[str]:
        guid = GUID.from_buffer_copy(uuid.UUID(GUID_DEVINTERFACE_USB_HUB).bytes_le)
        devices = self.setupapi.SetupDiGetClassDevsW(ct.byref(guid), None, None,
                                                    0x02 | 0x10)
        if devices == INVALID_HANDLE_VALUE:
            raise self._error("SetupDiGetClassDevsW")
        paths = []
        try:
            index = 0
            while True:
                interface = SP_DEVICE_INTERFACE_DATA()
                interface.cbSize = ct.sizeof(interface)
                if not self.setupapi.SetupDiEnumDeviceInterfaces(
                        devices, None, ct.byref(guid), index, ct.byref(interface)):
                    if ct.get_last_error() == ERROR_NO_MORE_ITEMS:
                        break
                    raise self._error("SetupDiEnumDeviceInterfaces")
                required = U32()
                ok = self.setupapi.SetupDiGetDeviceInterfaceDetailW(
                    devices, ct.byref(interface), None, 0, ct.byref(required), None)
                if ok or ct.get_last_error() != ERROR_INSUFFICIENT_BUFFER:
                    raise self._error("SetupDiGetDeviceInterfaceDetailW size query")
                if required.value < 8 or required.value > 65536:
                    raise ValueError("Invalid USB hub interface path size")
                detail = ct.create_string_buffer(required.value)
                # Windows WCHAR is 2 bytes; DevicePath begins at byte 4 even on
                # x64, while the ABI's cbSize is 8 on x64 and 6 on x86.
                U32.from_buffer(detail).value = 8 if ct.sizeof(HANDLE) == 8 else 6
                if not self.setupapi.SetupDiGetDeviceInterfaceDetailW(
                        devices, ct.byref(interface), detail, required,
                        ct.byref(required), None):
                    raise self._error("SetupDiGetDeviceInterfaceDetailW")
                path = detail.raw[4:].decode("utf-16-le").split("\0", 1)[0]
                paths.append(path)
                index += 1
        finally:
            self.setupapi.SetupDiDestroyDeviceInfoList(devices)
        return paths

    def open_hub(self, path: str):
        # FILE_ANY_ACCESS GET IOCTLs permit desired access 0. No write access
        # is requested; sharing permits existing applications to keep working.
        handle = self.kernel32.CreateFileW(path, 0, 0x01 | 0x02, None, 3, 0, None)
        if handle == INVALID_HANDLE_VALUE:
            raise self._error("CreateFileW USB hub")
        return handle

    def close_hub(self, handle):
        self.kernel32.CloseHandle(handle)

    def query(self, handle, code: int, initial: bytes) -> tuple[bytes, int]:
        allowed = (IOCTL_USB_GET_NODE_INFORMATION,
                   IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                   IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2)
        if code not in allowed:
            raise ValueError("Only read-only USB hub metadata IOCTLs are allowed")
        buffer = ct.create_string_buffer(initial, len(initial))
        returned = U32()
        if not self.kernel32.DeviceIoControl(handle, code, buffer, len(buffer),
                                            buffer, len(buffer),
                                            ct.byref(returned), None):
            raise self._error(f"DeviceIoControl 0x{code:08x}")
        if returned.value > len(buffer):
            raise ValueError("Hub driver returned an excessive byte count")
        return buffer.raw, returned.value


def probe(vid: int = 0x3654, pid: int = 0x5155, api=None) -> dict:
    result = {"timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
              "method": "Windows USB hub metadata GET IOCTLs",
              "filter": {"vid": f"{vid:04x}", "pid": f"{pid:04x}"},
              "hub_count": 0, "ports_queried": 0, "connected_device_count": 0,
              "matches": [], "errors": []}
    api = api or WindowsHubAPI()
    paths = api.hub_paths()
    result["hub_count"] = len(paths)
    for path in paths:
        handle = None
        try:
            handle = api.open_hub(path)
            raw, returned = api.query(handle, IOCTL_USB_GET_NODE_INFORMATION,
                                      bytes(NODE_INFORMATION_SIZE))
            raw = _checked_bytes(raw, returned, NODE_INFORMATION_SIZE)
            if struct.unpack_from("<I", raw)[0] != 0:
                raise ValueError("Interface did not return USB hub node metadata")
            ports = raw[6]  # NodeType(4) + descriptor length/type + port count
            for port in range(1, ports + 1):
                result["ports_queried"] += 1
                try:
                    initial = struct.pack("<I", port) + bytes(
                        CONNECTION_EX_SIZE + MAX_PIPES * PIPE_INFO_SIZE - 4)
                    raw, returned = api.query(
                        handle, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                        initial)
                    info = parse_connection_ex(raw, returned, port)
                    if info["connection_status"] != 1:
                        continue
                    result["connected_device_count"] += 1
                    descriptor = info["device_descriptor"]
                    if descriptor["vid"] != vid or descriptor["pid"] != pid:
                        continue
                    info["hub_path"] = path
                    v2 = None
                    try:
                        raw, returned = api.query(
                            handle, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                            struct.pack("<IIII", port, CONNECTION_V2_SIZE, 4, 0))
                        v2 = parse_connection_v2(raw, returned, port)
                        info["ex_v2"] = v2
                    except (OSError, ValueError) as exc:
                        info["ex_v2_error"] = str(exc)
                    info.update(decode_speed(info["raw_speed"], v2))
                    result["matches"].append(info)
                except (OSError, ValueError) as exc:
                    result["errors"].append({"hub_path": path, "port": port,
                                             "error": str(exc)})
        except (OSError, ValueError) as exc:
            result["errors"].append({"hub_path": path, "error": str(exc)})
        finally:
            if handle is not None:
                api.close_hub(handle)
    result["status"] = "matched" if result["matches"] else "not-found"
    if result["errors"]:
        result["status"] += "-with-errors"
    return result


def hex_id(text: str) -> int:
    try:
        value = int(text, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("Expected a hexadecimal USB ID") from exc
    if not 0 <= value <= 0xffff:
        raise argparse.ArgumentTypeError("USB ID must fit 16 bits")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=hex_id, default=0x3654)
    parser.add_argument("--pid", type=hex_id, default=0x5155)
    parser.add_argument("--out", type=Path, help="Save JSON evidence to this path")
    args = parser.parse_args()
    try:
        result = probe(args.vid, args.pid)
    except (OSError, ValueError) as exc:
        result = {"status": "probe-error", "errors": [str(exc)], "matches": []}
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    if result["status"] != "matched":
        return 1
    return 0 if all(match["speed_verified"] for match in result["matches"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
