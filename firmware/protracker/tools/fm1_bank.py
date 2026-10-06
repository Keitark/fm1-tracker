"""Canonical FM-1 Active Bank image tooling. Installation writes a PC file only."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import struct
import wave
import zlib

MAGIC = b"FM1BANK\0"
HEADER = struct.Struct("<8sHHHHIIII")
DIRECTORY = struct.Struct("<I20sIIIIIBbH")
MAX_ENTRIES = 256
TAGS = {"drum": 1, "break": 2, "bass": 4, "musical": 8, "fx": 16, "vocal": 32}


@dataclass(frozen=True)
class Entry:
    id: int
    name: str
    pcm: bytes
    volume: int = 64
    finetune: int = 0
    loop_start: int = 0
    loop_length: int = 0
    tags: int = 0

    def validate(self) -> None:
        name = self.name.encode("ascii")
        if not 0 < self.id <= 0xFFFFFFFF or not 0 < len(name) < 20 or any(c < 32 or c > 126 for c in name):
            raise ValueError("Entry requires nonzero 32-bit ID and printable ASCII name of 1..19 characters")
        n = len(self.pcm)
        if not 0 < n <= 131070 or n % 2:
            raise ValueError("PCM length must be even, 2..131070 bytes")
        if not 0 <= self.volume <= 64 or not -8 <= self.finetune <= 7 or not 0 <= self.tags <= 63:
            raise ValueError("Invalid volume, signed finetune or tag flags")
        if self.loop_start < 0 or self.loop_length < 0 or self.loop_start % 2 or self.loop_length % 2:
            raise ValueError("Loop addresses must be nonnegative and even")
        if self.loop_start + self.loop_length > n or (self.loop_length < 4 if self.loop_length else self.loop_start != 0):
            raise ValueError("Loop must fit PCM and be absent or at least four bytes")


def image_crc(data: bytes) -> int:
    return zlib.crc32(data[:28] + bytes(4) + data[32:]) & 0xFFFFFFFF


def build(entries: list[Entry]) -> bytes:
    if not 0 < len(entries) <= MAX_ENTRIES:
        raise ValueError("Bank requires 1..256 entries")
    entries = sorted(entries, key=lambda e: e.id)
    if len({e.id for e in entries}) != len(entries):
        raise ValueError("Duplicate stable ID")
    cursor = HEADER.size + len(entries) * DIRECTORY.size
    directory, payload = bytearray(), bytearray()
    for e in entries:
        e.validate()
        directory.extend(DIRECTORY.pack(e.id, e.name.encode("ascii"), cursor, len(e.pcm),
                                         e.loop_start, e.loop_length, e.tags, e.volume, e.finetune, 0))
        payload.extend(e.pcm)
        cursor += len(e.pcm)
    data = bytearray(HEADER.pack(MAGIC, 1, DIRECTORY.size, len(entries), 0, cursor, HEADER.size,
                                 HEADER.size + len(entries) * DIRECTORY.size, 0)) + directory + payload
    struct.pack_into("<I", data, 28, image_crc(data))
    return bytes(data)


def parse(data: bytes) -> list[Entry]:
    if len(data) < HEADER.size:
        raise ValueError("Truncated header")
    magic, version, entry_size, count, reserved, total, directory_offset, payload_offset, crc = HEADER.unpack_from(data)
    cursor = HEADER.size + count * DIRECTORY.size
    if (magic != MAGIC or version != 1 or entry_size != DIRECTORY.size or not 0 < count <= MAX_ENTRIES
            or reserved or total != len(data) or directory_offset != HEADER.size or payload_offset != cursor
            or cursor > len(data) or crc != image_crc(data)):
        raise ValueError("Invalid header or CRC")
    entries, prior_id = [], 0
    for index in range(count):
        id_, raw_name, offset, n, start, loop, tags, volume, tune, reserved = DIRECTORY.unpack_from(data, HEADER.size + index * DIRECTORY.size)
        nul = raw_name.find(b"\0")
        if id_ <= prior_id or offset != cursor or n > len(data) - cursor or reserved or nul <= 0 or any(raw_name[nul + 1:]):
            raise ValueError("Invalid directory, overlap or duplicate ID")
        try:
            e = Entry(id_, raw_name[:nul].decode("ascii"), data[offset:offset + n], volume, tune, start, loop, tags)
            e.validate()
        except (ValueError, UnicodeError) as exc:
            raise ValueError("Invalid entry metadata") from exc
        entries.append(e)
        cursor += n
        prior_id = id_
    if cursor != len(data):
        raise ValueError("Trailing PCM or missing directory entry")
    return entries


# Public validator used by the firmware builder before immutable embedding.
parse_bank = parse


def read_pcm(path: Path) -> bytes:
    if path.suffix.lower() != ".wav":
        data = path.read_bytes()  # .raw/.s8 contain signed eight-bit samples, unchanged.
    else:
        with wave.open(str(path), "rb") as wav:
            if wav.getcomptype() != "NONE" or wav.getnchannels() != 1 or wav.getsampwidth() not in (1, 2):
                raise ValueError("WAV must be uncompressed mono PCM with 8 or 16 bits")
            width, frames = wav.getsampwidth(), wav.getnframes()
            if frames > 131070:
                raise ValueError("WAV exceeds a MOD sample's 131070-byte limit")
            raw = wav.readframes(frames)
            if len(raw) != frames * width:
                raise ValueError("Truncated WAV PCM")
            if width == 1:
                data = bytes(v ^ 128 for v in raw)  # WAV PCM8 is unsigned.
            else:
                data = bytes((value >> 8) & 255 for (value,) in struct.iter_unpack("<h", raw))
    if len(data) % 2:
        data += b"\0"  # MOD lengths are measured in words.
    return data


def load_manifest(path: Path) -> list[Entry]:
    spec = json.loads(path.read_text(encoding="utf-8"))
    return [Entry(int(item["id"]), item["name"], read_pcm(path.parent / item["file"]),
                  int(item.get("volume", 64)), int(item.get("finetune", 0)),
                  int(item.get("loop_start", 0)), int(item.get("loop_length", 0)), int(item.get("tags", 0)))
            for item in spec["entries"]]


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    for operation in ("build", "install-image"):
        command = sub.add_parser(operation, help="Build a canonical file image from JSON; never writes device flash")
        command.add_argument("manifest", type=Path)
        command.add_argument("output", type=Path)
    command = sub.add_parser("list")
    command.add_argument("image", type=Path)
    for operation in ("add", "replace"):
        command = sub.add_parser(operation)
        command.add_argument("image", type=Path)
        command.add_argument("output", type=Path)
        command.add_argument("--id", type=int, required=True)
        command.add_argument("--name", required=True)
        command.add_argument("--pcm", type=Path, required=True)
        command.add_argument("--volume", type=int, default=64)
        command.add_argument("--finetune", type=int, default=0)
        command.add_argument("--loop-start", type=int, default=0)
        command.add_argument("--loop-length", type=int, default=0)
        command.add_argument("--tags", type=int, default=0)
    command = sub.add_parser("remove")
    command.add_argument("image", type=Path)
    command.add_argument("output", type=Path)
    command.add_argument("--id", type=int, required=True)
    args = parser.parse_args(argv)
    if args.operation in ("build", "install-image"):
        entries = load_manifest(args.manifest)
    else:
        entries = parse(args.image.read_bytes())
        if args.operation == "list":
            print(json.dumps([{k: v for k, v in vars(e).items() if k != "pcm"} | {"length": len(e.pcm)}
                              for e in entries], indent=2))
            return
        exists = any(e.id == args.id for e in entries)
        if args.operation in ("remove", "replace") and not exists:
            parser.error("Stable ID is absent")
        if args.operation == "add" and exists:
            parser.error("Stable ID already exists; use replace")
        entries = [e for e in entries if e.id != args.id]
        if args.operation != "remove":
            entries.append(Entry(args.id, args.name, read_pcm(args.pcm), args.volume, args.finetune,
                                 args.loop_start, args.loop_length, args.tags))
    data = build(entries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"Wrote PC bank image {args.output}: {len(entries)} entries, {len(data)} bytes; no device write")


if __name__ == "__main__":
    main()
