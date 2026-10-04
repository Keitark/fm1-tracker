"""Render exact offline FM-1 screenshots using the firmware's portable C view.

The host executable calls view.c, then applies the LCD's RGB444 truncation.
These are firmware renders; physical FM-1 display, input, and audio checks remain
pending until the candidate is flashed on a connected device.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[3]
OUTPUT = ROOT / "firmware/protracker/preview"
BUILD = ROOT / "firmware/protracker/build/host"
SONG = ROOT / "private-library/MOD/STATEART.MOD"
CAPTION = "Offline firmware render; physical FM-1 test pending."
SCREENS = {
    "playing-song": "PLAY SONG",
    "edit-audition": "EDIT / AUDITION",
    "write-note": "WRITE NOTE",
    "write-sample-parameters": "WRITE / SAMPLE PARAMETERS",
    "sample-list": "SAMPLE LIST",
    "usb-file-chooser": "USB FILE CHOOSER",
    "playing-pattern": "PATTERN LOOP",
    "boundary-row00": "ROW 00 / FIXED CURSOR",
    "boundary-row63": "ROW 63 / FIXED CURSOR",
}


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(command)}\n"
                           f"{result.stdout}\n{result.stderr}")
    return result


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def label_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    font = Path("C:/Windows/Fonts/segoeui.ttf")
    return ImageFont.truetype(str(font), size) if font.exists() else ImageFont.load_default()


def contact_sheet(screens: dict[str, Image.Image], output: Path) -> None:
    keys = ("playing-song", "edit-audition", "write-note", "write-sample-parameters")
    side, gap, heading, footer = 720, 32, 52, 70
    width = side * 2 + gap * 3
    height = (side + heading) * 2 + gap * 3 + footer
    sheet = Image.new("RGB", (width, height), "#171b22")
    pen = ImageDraw.Draw(sheet)
    font, small = label_font(28), label_font(22)
    for index, key in enumerate(keys):
        x = gap + index % 2 * (side + gap)
        y = gap + index // 2 * (side + heading + gap)
        pen.text((x, y), SCREENS[key], fill="#e9ebef", font=font)
        sheet.paste(screens[key].resize((side, side), Image.Resampling.NEAREST),
                    (x, y + heading))
    pen.text((gap, height - footer + 12), CAPTION, fill="#bcc6d3", font=small)
    sheet.save(output / "fm1-tracker-screens.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--song", type=Path, default=SONG)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--build", type=Path, default=BUILD)
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    song, output, build = args.song.resolve(), args.output.resolve(), args.build.resolve()
    if not args.no_build:
        run(["cmake", "--build", str(build), "--config", "Release",
             "--target", "mod_view_preview"])
    candidates = (build / "Release/mod_view_preview.exe", build / "mod_view_preview.exe",
                  build / "mod_view_preview")
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        raise FileNotFoundError(f"mod_view_preview executable missing in {build}")
    output.mkdir(parents=True, exist_ok=True)
    result = run([str(executable), str(song), str(output)])
    fields = ("page", "running", "write", "sample_mode", "order", "row", "channel", "sample")
    states = {}
    for line in result.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) != len(fields) + 1 or parts[0] not in SCREENS:
            raise ValueError(f"Unexpected preview metadata: {line}")
        states[parts[0]] = dict(zip(fields, map(int, parts[1:])))
    if set(states) != set(SCREENS):
        raise ValueError(f"Missing preview states: {set(SCREENS) - set(states)}")
    if states["boundary-row00"]["row"] != 0 or states["boundary-row63"]["row"] != 63:
        raise ValueError("Boundary cursor screenshots do not cover rows 00 and 63")

    screens = {}
    for key in SCREENS:
        with Image.open(output / f"{key}.ppm") as frame:
            image = frame.convert("RGB")
        if image.size != (240, 240) or len(image.getcolors(240 * 240) or []) < 3:
            raise ValueError(f"Invalid or blank firmware frame: {key}")
        if any(channel % 17 for pixel in image.getdata() for channel in pixel):
            raise ValueError(f"Frame did not use LCD RGB444 colors: {key}")
        image.save(output / f"{key}.png")
        image.resize((720, 720), Image.Resampling.NEAREST).save(output / f"{key}-3x.png")
        states[key]["sha256"] = sha256(output / f"{key}.png")
        screens[key] = image
    contact_sheet(screens, output)
    manifest = {
        "description": CAPTION,
        "renderer": "firmware/protracker/view.c",
        "renderer_sha256": sha256(ROOT / "firmware/protracker/view.c"),
        "mod_sha256": sha256(song),
        "geometry": [240, 240],
        "lcd_format": "RGB444 (upper bits truncated from RGB565)",
        "states": states,
    }
    (output / "preview-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                encoding="utf-8")
    print(output / "fm1-tracker-screens.png")


if __name__ == "__main__":
    main()
