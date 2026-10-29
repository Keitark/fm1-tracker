"""Serve a PC MOD collection to the FM-1 on-screen file chooser over USB CDC."""

from __future__ import annotations

import argparse
from contextlib import closing
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import re
import struct
import time
from uuid import uuid4

import client


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DIRS = (ROOT / "firmware/protracker/samples", ROOT / "private-library/MOD")
DISPLAY_NAMES = {
    "FM1START.MOD": "FM1 STARTER",
    "STATEART.MOD": "STATE OF THE ART",
    "3DDEMO2.MOD": "3D DEMO II",
    "9FINGERS.MOD": "9 FINGERS",
}


@dataclass(frozen=True)
class Song:
    path: Path
    name: str
    size: int


def validate_mod(data: bytes) -> None:
    """Mirror the firmware's bounds checks before advertising a PC file."""
    if not 2108 <= len(data) <= client.LIMIT or data[1080:1084] not in (b"M.K.", b"M!K!"):
        raise ValueError("unsupported MOD type or size")
    if not 1 <= data[950] <= 128 or any(x > 127 for x in data[952:1080]):
        raise ValueError("invalid order table")
    minimum = 1084 + 1024 * (max(data[952:1080]) + 1)
    if minimum > len(data):
        raise ValueError("truncated patterns")
    total = 0
    for i in range(31):
        h = 20 + i * 30
        size = struct.unpack_from(">H", data, h + 22)[0] * 2
        loop, repeat = (n * 2 for n in struct.unpack_from(">HH", data, h + 26))
        if data[h + 24] > 15 or data[h + 25] > 64:
            raise ValueError("invalid sample metadata")
        if size and (loop > size or (repeat > 2 and loop + repeat > size)):
            raise ValueError("invalid sample loop")
        total += size
    if total > len(data) - minimum:
        raise ValueError("truncated sample data")


def display_name(path: Path) -> str:
    name = DISPLAY_NAMES.get(path.name.upper(), path.stem.upper())
    name = "".join(c if c.isascii() and (c.isalnum() or c in " -.") else " " for c in name)
    return name[:20].strip() or "UNTITLED"


def discover(directories: list[Path]) -> list[Song]:
    paths = sorted({p.resolve() for directory in directories if directory.is_dir()
                    for p in directory.glob("*") if p.is_file() and p.suffix.lower() == ".mod"})
    songs = []
    for path in paths:
        data = path.read_bytes()
        validate_mod(data)
        songs.append(Song(path, display_name(path), len(data)))
    songs.sort(key=lambda song: (song.name != "FM1 STARTER", song.name))
    if not songs:
        raise ValueError("No compatible MOD files in the selected directories")
    if len(songs) > 32:
        raise ValueError("FM-1 file chooser supports at most 32 songs")
    return songs


def send_catalog(port, songs: list[Song]) -> None:
    if client.exchange(port, "LIB RESET") != "OK LIB RESET":
        raise RuntimeError("Catalog reset failed")
    for song in songs:
        encoded = song.name.ljust(20).encode("ascii").hex()
        if client.exchange(port, f"LIB ADD {song.size:08X} {encoded}") != "OK LIB ADD":
            raise RuntimeError(f"Catalog rejected {song.name}")
    if client.exchange(port, "LIB READY") != "OK LIB READY":
        raise RuntimeError("Catalog did not become ready")


def handle_pick(port, songs: list[Song], reply: str, autosave_dir: Path) -> Song | None:
    saving = re.fullmatch(r"LIB SAVE ([0-9A-F]{4})", reply)
    if saving:
        ticket = saving[1]
        try:
            autosave_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            destination = autosave_dir / f"fm1-song-{stamp}-{uuid4().hex[:8]}.mod"
            size, crc = client.save(port, destination)
            if client.exchange(port, f"LIB DONE {ticket}") != "OK LIB DONE":
                raise RuntimeError("Song is saved on PC, but request acknowledgement failed")
            print(f"Saved FM-1 song to {destination}: {size} bytes, CRC32 {crc:08X}", flush=True)
            return None
        except Exception:
            try:
                client.exchange(port, f"LIB FAIL {ticket}")
            except Exception:
                pass
            raise
    match = re.fullmatch(r"LIB PICK ([0-9A-F]{4}) ([0-9A-F]{2})", reply)
    if not match:
        if reply == "LIB IDLE":
            return None
        raise RuntimeError(f"Unexpected library response: {reply}")
    ticket, index = match[1], int(match[2], 16)
    if index >= len(songs):
        raise RuntimeError(f"Device selected invalid library index {index}")
    song = songs[index]
    try:
        state = client.exchange(port, "MOD STATUS")
        if "dirty=1" in state:
            autosave_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            destination = autosave_dir / f"fm1-edit-{stamp}-{uuid4().hex[:8]}.mod"
            client.save(port, destination)
            print(f"Saved FM-1 edits to {destination}", flush=True)
        size, crc = client.upload(port, song.path)
        if client.exchange(port, f"LIB DONE {ticket}") != "OK LIB DONE":
            raise RuntimeError("Device did not acknowledge library load")
        print(f"Loaded {song.name}: {size} bytes, CRC32 {crc:08X}", flush=True)
        return song
    except Exception:
        try:
            client.exchange(port, f"LIB FAIL {ticket}")
        except Exception:
            pass
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("list", "serve"))
    parser.add_argument("--port", help="FM-1 CDC COM port for serve")
    parser.add_argument("--library", type=Path, action="append", help="MOD directory; repeatable")
    parser.add_argument("--autosave-dir", type=Path, default=ROOT / "private-library/MOD/edits")
    args = parser.parse_args()
    songs = discover(args.library or list(DEFAULT_DIRS))
    for index, song in enumerate(songs):
        print(f"{index:02d}  {song.name:<20} {song.size:>7} bytes  {song.path}")
    if args.action == "list":
        return
    if not args.port:
        parser.error("--port is required for serve")
    from serial import Serial
    with closing(Serial(port=None, baudrate=115200, timeout=2, write_timeout=5)) as port:
        port.port = args.port
        port.dtr = True
        port.rts = False
        port.open()
        port.reset_input_buffer()
        client.identify(port)
        send_catalog(port, songs)
        print("On-device file chooser ready. Press Ctrl+C to stop the PC service.", flush=True)
        while True:
            reply = client.exchange(port, "LIB POLL")
            try:
                handle_pick(port, songs, reply, args.autosave_dir)
            except (OSError, RuntimeError, ValueError, TimeoutError) as error:
                # A full disk or rejected load must leave the service available
                # for a deliberate retry on the device.
                print(f"FM-1 request failed: {error}", flush=True)
            time.sleep(.2)


if __name__ == "__main__":
    main()
