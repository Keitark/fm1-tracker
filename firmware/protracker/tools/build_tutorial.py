"""Build the standalone graphical guide. Requires markdown-it-py.

Run from the repository root: python firmware/protracker/tools/build_tutorial.py
"""
from __future__ import annotations

import base64
from pathlib import Path
import re
import unicodedata

from markdown_it import MarkdownIt

ROOT = Path(__file__).resolve().parents[1]


def slug(text: str) -> str:
    text = unicodedata.normalize("NFKC", text).lower()
    return re.sub(r"[^\w\- ]", "", text).replace(" ", "-")


def main() -> None:
    renderer = MarkdownIt("commonmark").enable("table")
    tokens = renderer.parse((ROOT / "TUTORIAL.md").read_text(encoding="utf-8"))
    for i, token in enumerate(tokens):
        if token.type == "heading_open":
            token.attrSet("id", slug(tokens[i + 1].content))
    reference = renderer.renderer.render(tokens, renderer.options, {})

    def embed_image(match: re.Match[str]) -> str:
        path = ROOT / match[1]
        return 'src="data:image/png;base64,' + base64.b64encode(path.read_bytes()).decode() + '"'

    reference = re.sub(r'src="(preview/[^\"]+\.png)"', embed_image, reference)
    reference = reference.replace('href="README.md"',
        'href="https://github.com/Keitark/fm1-tracker/blob/feat/2-fast-tracker-editing/firmware/protracker/README.md"')
    svg = (ROOT / "preview/fm1-control-map.svg").read_text(encoding="utf-8")
    static_svg = svg.replace('id="', 'id="manual-').replace('url(#', 'url(#manual-')
    static_svg = static_svg.replace('aria-labelledby="fm1-map-title fm1-map-desc"',
                                    'aria-labelledby="manual-fm1-map-title manual-fm1-map-desc"')
    static_svg = re.sub(r' (?:data-control|data-label|tabindex)="[^\"]*"', '', static_svg)
    static_svg = static_svg.replace(' role="button"', '')
    reference = reference.replace('</h1>', '</h1>' + static_svg +
        '<p class="map-caption">FM-1 control locations. Added piano labels show OC−1 pitches.</p>', 1)
    svg = svg.replace('role="img"', 'role="group"', 1)
    shell = (ROOT / "tools/tutorial_shell.html").read_text(encoding="utf-8")
    output = shell.replace("<!-- DEVICE_SVG -->", svg).replace("<!-- REFERENCE_HTML -->", reference)
    if "<!-- DEVICE_SVG -->" in output or "<!-- REFERENCE_HTML -->" in output:
        raise ValueError("Unexpanded template")
    (ROOT / "TUTORIAL.html").write_text(output, encoding="utf-8", newline="\n")
    print(f"Built {ROOT / 'TUTORIAL.html'} ({len(output.encode('utf-8')):,} bytes)")


if __name__ == "__main__":
    main()
