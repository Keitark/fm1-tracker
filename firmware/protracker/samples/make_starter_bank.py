"""Build an original, compact, signed 8-bit mono ProTracker instrument bank."""

from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import random
import struct


RATE_AT_C2 = 7_093_789.2 / (2 * 428)  # Paula PAL clock at MOD period 428
LOOP = 128
INTRO = 384
TWO_PI = 2 * math.pi


def sample_byte(value: float) -> int:
    return max(-127, min(127, round(value * 116))) & 0xFF


def pitched(kind: str) -> tuple[bytes, int, int]:
    data = bytearray()
    for i in range(INTRO + LOOP):
        phase = TWO_PI * (i % LOOP) / LOOP
        s = math.sin(phase)
        if kind == "sine":
            value = s
        elif kind == "triangle":
            value = 2 / math.pi * math.asin(s)
        elif kind == "saw":
            value = 2 * ((i % LOOP) / LOOP) - 1
        elif kind == "square":
            value = 1 if i % LOOP < LOOP // 2 else -1
        elif kind == "pulse":
            value = 1 if i % LOOP < LOOP // 4 else -1
        elif kind == "organ":
            value = .62 * s + .24 * math.sin(2 * phase) + .14 * math.sin(3 * phase)
        elif kind == "reed":
            value = .55 * s + .27 * math.sin(3 * phase) + .18 * math.sin(5 * phase)
        elif kind == "brass":
            value = .44 * s + .29 * math.sin(2 * phase) + .18 * math.sin(3 * phase) + .09 * math.sin(4 * phase)
        elif kind == "pad":
            value = .69 * s + .20 * math.sin(2 * phase) + .11 * math.sin(4 * phase)
        elif kind == "lead":
            value = .62 * (1 if i % LOOP < LOOP // 2 else -1) + .38 * s
        elif kind == "bass":
            value = .81 * s + .19 * math.sin(2 * phase)
        else:
            raise ValueError(kind)
        attack = min(1.0, (i + 1) / 32)
        data.append(sample_byte(value * attack * .8))
    return bytes(data), INTRO, LOOP


def one_shot(kind: str, length: int, seed: int) -> tuple[bytes, int, int]:
    rng = random.Random(seed)
    data = bytearray()
    noise_lp = 0.0
    for i in range(length):
        t = i / RATE_AT_C2
        u = i / length
        noise = rng.uniform(-1, 1)
        noise_lp = .72 * noise_lp + .28 * noise
        if kind == "pluck":
            phase = TWO_PI * 65 * t
            value = (.70 * math.sin(phase) + .22 * math.sin(2 * phase)
                     + .08 * math.sin(3 * phase)) * math.exp(-5.2 * u)
        elif kind == "kick":
            phase = TWO_PI * (48 * t + 95 * .028 * (1 - math.exp(-t / .028)))
            value = math.sin(phase) * math.exp(-8 * u)
        elif kind == "snare":
            value = (.76 * noise + .24 * math.sin(TWO_PI * 175 * t)) * math.exp(-7 * u)
        elif kind == "closed_hat":
            value = (noise - noise_lp) * 1.7 * math.exp(-12 * u)
        elif kind == "open_hat":
            value = (noise - noise_lp) * 1.45 * math.exp(-5 * u)
        elif kind == "clap":
            bursts = sum(math.exp(-((t - center) / .011) ** 2) for center in (.0, .027, .056, .09))
            value = noise * min(1.0, bursts) * math.exp(-2.5 * u)
        else:
            raise ValueError(kind)
        data.append(sample_byte(value * .8))
    return bytes(data), 0, 2  # MOD's one-word repeat means no audible loop


INSTRUMENTS = [
    ("01 SINE", "sine", 0),
    ("02 TRIANGLE", "triangle", 0),
    ("03 SAW", "saw", 0),
    ("04 SQUARE", "square", 0),
    ("05 PULSE", "pulse", 0),
    ("06 ORGAN", "organ", 0),
    ("07 REED", "reed", 0),
    ("08 BRASS", "brass", 0),
    ("09 SOFT PAD", "pad", 0),
    ("10 CHIP LEAD", "lead", 0),
    ("11 BASS", "bass", 0),
    ("12 PLUCK", "pluck", 8192),
    ("13 KICK", "kick", 4096),
    ("14 SNARE", "snare", 4096),
    ("15 CLOSED HAT", "closed_hat", 1536),
    ("16 OPEN HAT", "open_hat", 4096),
    ("17 CLAP", "clap", 3072),
]


def build() -> bytes:
    header = bytearray(1084)
    header[:20] = b"FM1 8BIT SAMPLE BANK"
    header[950] = 1  # one blank pattern, ready for note entry
    header[1080:1084] = b"M.K."
    pcm = bytearray()
    for number, (name, kind, length) in enumerate(INSTRUMENTS, 1):
        sample, loop_start, loop_length = (
            one_shot(kind, length, number) if length else pitched(kind)
        )
        assert len(sample) % 2 == loop_start % 2 == loop_length % 2 == 0
        assert 0 < len(sample) <= 131070
        assert loop_start + loop_length <= len(sample)
        h = 20 + (number - 1) * 30
        header[h:h + len(name)] = name.encode("ascii")
        struct.pack_into(">H", header, h + 22, len(sample) // 2)
        header[h + 25] = 48 if kind in {"square", "pulse", "lead"} else 64
        struct.pack_into(">HH", header, h + 26, loop_start // 2, loop_length // 2)
        pcm.extend(sample)
    result = bytes(header) + bytes(1024) + bytes(pcm)
    assert len(result) <= 192 * 1024
    return result


def verify(data: bytes) -> None:
    assert data[1080:1084] == b"M.K." and data[950] == 1
    assert data[1084:2108] == bytes(1024)
    assert len(data) <= 192 * 1024
    offset = 2108
    for number, (name, _, _) in enumerate(INSTRUMENTS, 1):
        h = 20 + (number - 1) * 30
        assert data[h:h + len(name)] == name.encode("ascii")
        size = struct.unpack_from(">H", data, h + 22)[0] * 2
        loop, repeat = struct.unpack_from(">HH", data, h + 26)
        assert size > 0 and loop * 2 + repeat * 2 <= size
        assert 0 < data[h + 25] <= 64
        assert any(data[offset:offset + size])
        offset += size
    assert offset == len(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("FM1START.MOD"))
    args = parser.parse_args()
    data = build()
    verify(data)
    args.output.write_bytes(data)
    print(f"{args.output}: {len(data)} bytes, {len(INSTRUMENTS)} samples, SHA256 {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
