"""Generate the original FM1BREAK bank and tutorial MOD without external audio."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from fm1_bank import Entry, TAGS, build as build_bank, parse

RATE = 7093789.2 / (2 * 428)  # Native PCM playback at PAL Paula period 428.
TAU = 2 * math.pi
PALETTE = [
    (101, "TIGHT KICK", "kick", 1536, "drum"),
    (102, "DEEP KICK", "deep", 2048, "drum"),
    (103, "DRY SNARE", "snare", 2048, "drum"),
    (104, "CRUNCH SNARE", "crunch", 2048, "drum"),
    (105, "CLAP", "clap", 2048, "drum"),
    (106, "CLOSED HAT", "hat", 768, "drum"),
    (107, "OPEN HAT", "open", 2048, "drum"),
    (108, "METAL RIDE", "ride", 3072, "drum"),
    (109, "RIM CLICK", "rim", 512, "drum"),
    (110, "LOW TOM", "tom", 1024, "drum"),
    (111, "HIGH TOM", "tomhi", 768, "drum"),
    (112, "SHAKER", "shaker", 1024, "drum"),
    (201, "CHOP KICK HAT", "chop1", 4096, "break"),
    (202, "CHOP SNARE GHOST", "chop2", 4096, "break"),
    (203, "CHOP RIM SHUFFLE", "chop3", 4096, "break"),
    (204, "CHOP TOM TURN", "chop4", 4096, "break"),
    (301, "SUB BASS", "sub", 2048, "bass"),
    (302, "SAW BASS", "saw", 2048, "bass"),
    (303, "DIRTY REESE", "reese", 2048, "bass"),
    (401, "EP MAJ7", "maj7", 4096, "musical"),
    (402, "EP MIN7", "min7", 4096, "musical"),
    (403, "RAVE CHORD", "rave", 4096, "musical"),
    (404, "ORGAN PLUCK", "organ", 4096, "musical"),
    (405, "ORCHESTRA HIT", "orch", 4096, "musical"),
    (501, "REVERSE SWEEP", "reverse", 2048, "fx"),
    (502, "SCRATCH ZIP", "scratch", 2048, "fx"),
    (601, "COME ON!", "comeon", 8192, "vocal"),
    (602, "HEY!", "hey", 2048, "vocal"),
    (603, "YEAH!", "yeah", 4096, "vocal"),
]


def quantize(values: list[float]) -> bytes:
    peak = max(abs(value) for value in values) or 1
    result = bytearray(max(-127, min(127, round(value / peak * 108))) & 255 for value in values)
    result[0] = result[-1] = 0  # Remove boundary clicks on one-shots.
    return bytes(result)


def drum(kind: str, length: int, seed: int) -> list[float]:
    rng, low = random.Random(seed), 0.0
    out = []
    for i in range(length):
        t, u = i / RATE, i / length
        noise = rng.uniform(-1, 1)
        old_low, low = low, .68 * low + .32 * noise
        high = noise - old_low
        if kind in ("kick", "deep"):
            floor = 46 if kind == "kick" else 36
            phase = TAU * (floor * t + 96 * .021 * (1 - math.exp(-t / .021)))
            value = (math.sin(phase) + .07 * high * math.exp(-t * 95)) * math.exp(-u * 7)
        elif kind in ("snare", "crunch"):
            value = (.74 * noise + .26 * math.sin(TAU * 172 * t)) * math.exp(-u * 7)
            if kind == "crunch":
                value = math.tanh(value * 3) * math.exp(-u * 2)
        elif kind == "clap":
            pulses = sum(math.exp(-((t - center) / .008) ** 2) for center in (.007, .027, .049, .075))
            value = high * (pulses + .17 * math.exp(-t * 12)) * math.exp(-u * 2)
        elif kind in ("hat", "open", "shaker"):
            decay = {"hat": 13, "open": 5, "shaker": 8}[kind]
            value = high * math.exp(-u * decay) * (1 if kind != "shaker" else math.sin(math.pi * min(1, u * 10)))
        elif kind == "ride":
            value = sum(math.sin(TAU * hz * t) for hz in (731, 1187, 1721, 2437)) * .2 * math.exp(-u * 4) + high * .13 * math.exp(-u * 7)
        elif kind == "rim":
            value = (math.sin(TAU * 1037 * t) + .4 * math.sin(TAU * 1733 * t)) * math.exp(-u * 13)
        elif kind in ("tom", "tomhi"):
            base = 95 if kind == "tom" else 155
            value = math.sin(TAU * (base * t + 24 * .045 * (1 - math.exp(-t / .045)))) * math.exp(-u * 6)
        else:
            raise ValueError(kind)
        out.append(value)
    return out


def voice(kind: str, length: int, seed: int) -> list[float]:
    # Original robotic formant synthesis: no speech recording, TTS model or sampled vocal.
    rng, out = random.Random(seed), []
    for i in range(length):
        t, u = i / RATE, i / length
        if kind == "comeon":
            consonant = u < .055
            forms = (650, 1120, 2450) if u < .35 else (430, 870, 2600)
            nasal = .30 < u < .40 or u > .78
        elif kind == "hey":
            consonant = u < .10
            forms, nasal = (530 + u * 160, 1900 - u * 400, 2600), False
        else:
            consonant = False
            forms, nasal = (350 + u * 450, 2200 - u * 900, 2650), False
        f0 = 115 + 12 * math.sin(u * math.pi) - 16 * u
        value = 0.0
        for harmonic in range(1, 30):
            hz = harmonic * f0
            weight = sum(math.exp(-((hz - f) / 125) ** 2) for f in forms)
            if nasal:
                weight = math.exp(-((hz - 250) / 130) ** 2) * 2 + .12 * weight
            value += math.sin(TAU * hz * t) * weight / math.sqrt(harmonic)
        if consonant:
            value = rng.uniform(-1, 1) * .42
        envelope = min(1, u * 50) * min(1, (1 - u) * 14)
        if kind == "comeon":
            envelope *= .25 if .39 < u < .44 else 1
        out.append(value * envelope)
    return out


def synth(kind: str, length: int, seed: int) -> tuple[bytes, int, int]:
    if kind.startswith("chop"):
        pairs = {"chop1": [(0, "kick"), (1420, "hat"), (2780, "rim")],
                 "chop2": [(0, "snare"), (1900, "rim"), (3050, "hat")],
                 "chop3": [(0, "rim"), (1010, "hat"), (2600, "snare")],
                 "chop4": [(0, "tom"), (1290, "tomhi"), (2870, "kick")]}
        values = [0.0] * length
        for n, (offset, hit) in enumerate(pairs[kind]):
            for j, value in enumerate(drum(hit, min(1536, length - offset), seed + n)):
                values[offset + j] += value
        return quantize(values), 0, 0
    if kind in {"kick", "deep", "snare", "crunch", "clap", "hat", "open", "ride", "rim", "tom", "tomhi", "shaker"}:
        return quantize(drum(kind, length, seed)), 0, 0
    if kind in ("comeon", "hey", "yeah"):
        return quantize(voice(kind, length, seed)), 0, 0
    rng, values = random.Random(seed), []
    for i in range(length):
        t, u, phase = i / RATE, i / length, TAU * i / 128
        if kind == "sub":
            value = .90 * math.sin(phase) + .10 * math.sin(2 * phase)
        elif kind == "saw":
            value = sum(math.sin(k * phase) / k for k in range(1, 10))
        elif kind == "reese":
            value = sum((math.sin(k * phase) + .65 * math.sin(k * phase + TAU * i / 1024)) / k for k in range(1, 7))
        elif kind in ("maj7", "min7", "rave"):
            notes = {"maj7": (0, 4, 7, 11), "min7": (0, 3, 7, 10), "rave": (0, 7, 12, 15)}[kind]
            value = 0
            for note in notes:
                hz = 261.625565 * 2 ** (note / 12)
                fundamental = math.sin(TAU * hz * t)
                value += fundamental + (.25 if kind != "rave" else .65) * math.sin(TAU * hz * 2 * t)
            value *= math.exp(-u * (5 if kind != "rave" else 8))
        elif kind == "organ":
            value = (math.sin(phase * 4) + .3 * math.sin(phase * 8) + .16 * math.sin(phase * 12)) * math.exp(-u * 5)
        elif kind == "orch":
            value = sum(math.sin(TAU * hz * t) * math.exp(-u * (3 + n * .2)) / (n + 1) for n, hz in enumerate((196, 261.626, 329.628, 392, 523.251, 783.991)))
        elif kind == "reverse":
            value = (rng.uniform(-1, 1) * .65 + math.sin(TAU * (80 * t + 1900 * t * t))) * u ** 2
        elif kind == "scratch":
            value = math.sin(TAU * (220 * t + 2300 * t * t)) * math.sin(u * math.pi * 4) * math.exp(-u * 3) + rng.uniform(-1, 1) * .10
        else:
            raise ValueError(kind)
        value *= min(1, (i + 1) / 24)
        values.append(value)
    pcm = quantize(values)
    if kind in ("sub", "saw", "reese"):
        # All harmonics/detuning have exact period1024; restore boundary sample for loop continuity.
        copy = bytearray(pcm)
        copy[-1] = max(-127, min(127, round(values[-1] / max(abs(v) for v in values) * 108))) & 255
        return bytes(copy), 1024, 1024
    return pcm, 0, 0


def entries() -> list[Entry]:
    result = []
    for id_, name, kind, length, tag in PALETTE:
        pcm, start, loop = synth(kind, length, id_)
        volume = 48 if tag in ("musical", "vocal") else 56
        result.append(Entry(id_, name, pcm, volume, 0, start, loop, TAGS[tag]))
    return result


ORDER = [0, 1, 1, 2, 3, 1, 4, 4, 2, 5]
SECTIONS = ["INTRO", "BREAK A", "FILL", "BREAK B", "DROP", "OUTRO"]


def build_demo(bank_entries: list[Entry]) -> bytes:
    header, pcm = bytearray(1084), bytearray()
    title = b"FM1 ORIGINAL BREAK"
    header[:len(title)] = title
    header[950], header[951] = len(ORDER), 127
    header[952:952 + len(ORDER)] = bytes(ORDER)
    header[1080:1084] = b"M.K."
    slots = {}
    for slot, entry in enumerate(bank_entries, 1):
        slots[entry.id] = slot
        h = 20 + (slot - 1) * 30
        name = entry.name.encode("ascii")
        header[h:h + len(name)] = name
        struct.pack_into(">HBBHH", header, h + 22, len(entry.pcm) // 2,
                         entry.finetune & 15, entry.volume, entry.loop_start // 2,
                         entry.loop_length // 2 if entry.loop_length else 1)
        pcm.extend(entry.pcm)
    patterns = bytearray(len(SECTIONS) * 1024)

    def put(pattern: int, row: int, channel: int, id_: int = 0, note: int = 48, effect: int = 0, param: int = 0) -> None:
        period = round(428 * 2 ** ((48 - note) / 12)) if id_ else 0
        slot = slots[id_] if id_ else 0
        offset = pattern * 1024 + row * 16 + channel * 4
        patterns[offset:offset + 4] = bytes(((slot & 0xF0) | (period >> 8), period & 255,
                                            (slot & 15) << 4 | effect, param))

    for pattern in range(len(SECTIONS)):
        for row in range(0, 64, 8):
            if pattern != 5 or row < 32:
                drum_id = 101 if row % 16 == 0 else 103
                if pattern in (1, 3, 4):
                    drum_id = (201, 202, 203, 204)[(row // 8 + pattern) % 4]
                put(pattern, row, 0, drum_id, effect=9 if pattern in (1, 3) else 0,
                    param=4 if pattern in (1, 3) and row % 16 == 8 else 0)
                if pattern != 0 or row >= 16:
                    bass_id = 301 if pattern in (0, 5) else 303
                    put(pattern, row, 1, bass_id, (48, 48, 51, 55)[row // 8 % 4])
        for row in range(2, 64, 4):
            if pattern != 5 or row < 32:
                put(pattern, row, 2, 106, effect=0xE if row % 16 == 14 else 0,
                    param=0x93 if row % 16 == 14 else 0)
        for row in (0, 24, 40):
            if pattern != 5 or row < 32:
                put(pattern, row, 3, 401 if pattern in (0, 1, 5) else 403, 48 if row != 24 else 51)
        if pattern in (1, 3, 4):
            put(pattern, 56, 3, 601, effect=0xA, param=0x03)
        if pattern == 2:
            for row in range(48, 60, 2):
                put(pattern, row, 0, 110 if row % 4 == 0 else 111, effect=0xE, param=0x92)
            put(pattern, 59, 3, effect=0xD, param=0)
        if pattern == 4:
            put(pattern, 16, 3, 405)
            put(pattern, 48, 3, 502, effect=9, param=2)
        if pattern == 5:
            put(pattern, 32, 3, 501)
            put(pattern, 48, 3, effect=0xD, param=0)
    # First row sets160BPM andspeed6; chords begin onrow4 to retain these commands.
    put(0, 0, 3, effect=0xF, param=160)
    put(0, 1, 3, effect=0xF, param=6)
    put(0, 4, 3, 401)
    result = bytes(header + patterns + pcm)
    assert len(result) < 256 * 1024
    return result


def generate(output: Path) -> dict:
    bank_entries = entries()
    bank, demo = build_bank(bank_entries), build_demo(bank_entries)
    assert 64 * 1024 <= len(bank) <= 96 * 1024 and 24 <= len(bank_entries) <= 32
    assert parse(bank) == bank_entries
    output.mkdir(parents=True, exist_ok=True)
    (output / "FM1BREAK.FMB").write_bytes(bank)
    (output / "FM1BREAK.MOD").write_bytes(demo)
    metadata = {"generator": "default_bank.py", "algorithm_version": 1,
                "provenance": "Original mathematical synthesis; no recordings, pretrained voice or third-party sample assets",
                "asset_license": "CC0-1.0", "pcm": "signed8 mono", "native_rate_at_period428": RATE,
                "bank_bytes": len(bank), "bank_sha256": hashlib.sha256(bank).hexdigest(),
                "demo_bytes": len(demo), "demo_sha256": hashlib.sha256(demo).hexdigest(),
                "demo_bpm": 160, "pattern_names": SECTIONS, "orders": ORDER,
                "entries": [{"slot": slot, "id": e.id, "name": e.name, "length": len(e.pcm),
                             "volume": e.volume, "finetune": e.finetune, "loop_start": e.loop_start,
                             "loop_length": e.loop_length, "tags": e.tags,
                             "sha256": hashlib.sha256(e.pcm).hexdigest(),
                             "voice_style": "procedural robotic formants" if e.tags & TAGS["vocal"] else None}
                            for slot, e in enumerate(bank_entries, 1)]}
    (output / "FM1BREAK.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    metadata = generate(args.output_dir)
    print(json.dumps({k: metadata[k] for k in ("bank_bytes", "bank_sha256", "demo_bytes", "demo_sha256")}, indent=2))


if __name__ == "__main__":
    main()
