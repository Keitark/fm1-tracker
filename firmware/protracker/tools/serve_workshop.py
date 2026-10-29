"""Run the actual FM-1 tracker controller and renderer as a local PC workshop.

No serial/MIDI connection, firmware writes, or automatic song-file writes.
Build tracker_sim with CMake, then run this script and open the printed URL.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import threading
import time
import shutil
from urllib.parse import urlparse

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class Workshop:
    def __init__(self, library: Path, song: Path, bank: Path):
        # Windows holds a loaded DLL open. Run a content-addressed copy so
        # rebuilding the firmware host library does not interrupt this session.
        runtime = ROOT / "build/workshop-runtime"
        runtime.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256(library.read_bytes()).hexdigest()
        copy = runtime / ("tracker_sim-" + digest + library.suffix)
        if not copy.exists():
            shutil.copy2(library, copy)
        if hashlib.sha256(copy.read_bytes()).hexdigest() != digest:
            raise ValueError("Workshop runtime copy differs from built library")
        self.api = C.CDLL(str(copy.resolve()))
        self.api.fm1_sim_init.argtypes = [C.c_void_p, C.c_uint, C.c_void_p, C.c_uint]
        self.api.fm1_sim_key.argtypes = [C.c_uint, C.c_uint, C.c_uint32]
        self.api.fm1_sim_encoder.argtypes = [C.c_uint, C.c_int, C.c_uint32]
        self.api.fm1_sim_audio.argtypes = [C.c_void_p, C.c_uint, C.c_uint32]
        self.api.fm1_sim_frame.argtypes = [C.c_void_p, C.c_uint, C.c_uint32]
        self.api.fm1_sim_state.argtypes = [C.c_void_p, C.c_uint]
        self.api.fm1_sim_export.argtypes = [C.c_void_p, C.c_uint]
        mod, pcm = song.read_bytes(), bank.read_bytes()
        if self.api.fm1_sim_init(mod, len(mod), pcm, len(pcm)):
            raise ValueError("Simulator rejected the MOD or Active Bank")
        self.lock = threading.RLock()
        self.started = self.previous = time.monotonic()
        self.sound = bytearray()

    def now(self) -> int:
        return int((time.monotonic() - self.started) * 1000) & 0xFFFFFFFF

    def advance(self):
        current = time.monotonic()
        frames = min(11025, int((current - self.previous) * 44100))
        self.previous = current
        while frames:
            count = min(frames, 4410)
            samples = (C.c_int16 * (count * 2))()
            self.api.fm1_sim_audio(samples, count, self.now())
            self.sound.extend(bytes(samples))
            frames -= count
        if len(self.sound) > 44100:
            del self.sound[:-44100]

    def state(self):
        buffer = C.create_string_buffer(1024)
        self.api.fm1_sim_state(buffer, len(buffer))
        return json.loads(buffer.value)

    def frame(self) -> bytes:
        pixels = C.create_string_buffer(240 * 240 * 3)
        if self.api.fm1_sim_frame(pixels, len(pixels), self.now()):
            raise RuntimeError("Renderer failed")
        output = io.BytesIO()
        Image.frombytes("RGB", (240, 240), pixels.raw).save(output, "PNG")
        return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/workflow20")
    parser.add_argument("--song", type=Path, default=ROOT / "samples/FM1BREAK.MOD")
    parser.add_argument("--bank", type=Path, default=ROOT / "samples/FM1BREAK.FMB")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    names = ("Release/tracker_sim.dll", "tracker_sim.dll", "libtracker_sim.so", "libtracker_sim.dylib")
    library = next((args.build / name for name in names if (args.build / name).is_file()), None)
    if library is None:
        parser.error("Build the tracker_sim CMake target first")
    workshop = Workshop(library, args.song, args.bank)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send(self, data: bytes, content_type="application/json", status=200):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Cross-Origin-Resource-Policy", "same-origin")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            route = urlparse(self.path).path
            if route == "/":
                return self.send((ROOT / "tools/workshop.html").read_bytes(), "text/html; charset=utf-8")
            with workshop.lock:
                workshop.advance()
                if route == "/frame.png":
                    return self.send(workshop.frame(), "image/png")
                if route == "/state":
                    return self.send(json.dumps(workshop.state()).encode())
                if route == "/audio.pcm":
                    sound = bytes(workshop.sound)
                    workshop.sound.clear()
                    return self.send(sound, "application/octet-stream")
                if route == "/song.mod":
                    output = C.create_string_buffer(256 * 1024)
                    length = workshop.api.fm1_sim_export(output, len(output))
                    if length:
                        return self.send(output.raw[:length], "application/octet-stream")
            self.send(b'{"error":"Not found"}', status=404)

        def do_POST(self):
            # Local browser only: cross-origin pages cannot operate the session.
            expected = f"http://127.0.0.1:{args.port}"
            if self.headers.get("Origin") not in (None, expected):
                return self.send(b'{"error":"Origin rejected"}', status=403)
            try:
                if urlparse(self.path).path != "/input":
                    raise ValueError("Unknown action route")
                size = int(self.headers.get("Content-Length", "0"))
                if not 1 <= size <= 256:
                    raise ValueError("Invalid input size")
                message = json.loads(self.rfile.read(size))
                with workshop.lock:
                    workshop.advance()
                    if message.get("kind") == "key":
                        slot, down = int(message["id"]), int(message["down"])
                        if not 0 <= slot < 41 or down not in (0, 1):
                            raise ValueError("Invalid key")
                        workshop.api.fm1_sim_key(slot, down, workshop.now())
                    elif message.get("kind") == "encoder":
                        encoder, step = int(message["id"]), int(message["step"])
                        if not 0 <= encoder < 7 or step not in (-1, 1):
                            raise ValueError("One encoder detent per action")
                        workshop.api.fm1_sim_encoder(encoder, step, workshop.now())
                    else:
                        raise ValueError("Unknown input")
                    return self.send(json.dumps(workshop.state()).encode())
            except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
                return self.send(json.dumps({"error": str(error)}).encode(), status=400)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"FM-1 tracker workshop: http://127.0.0.1:{args.port}", flush=True)
    print("Offline simulator. No FM-1 is connected or changed by this process.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
