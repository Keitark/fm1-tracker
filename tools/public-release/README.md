# Public source candidate boundary

This tool prepares a **candidate requiring manual review**, not a public release.
It never publishes, copies Git history, builds firmware, opens a device, fetches
dependencies or changes upstream licenses. The initial scope is independently
authored UTF-8 source below `firmware/usb-midi/` only. The separately owner-
relicensed Apache USB/audio component remains outside that bounded scope until
its wider dependency closure is separately prepared. Unrelated GPL engines,
SDKs, private images and music/game assets are also excluded.

`candidate-inventory.json` explicitly classifies every source file in that
directory; an empty or incomplete inventory fails audit. Each entry contains:

```json
{
  "path": "firmware/usb-midi/example.c",
  "sha256": "EXACT_64_CHARACTER_LOWERCASE_SHA256",
  "license": "Apache-2.0",
  "origin": {
    "kind": "independently-authored",
    "description": "Project-authored implementation; describe factual protocol references and any source consulted."
  }
}
```

The current new independent source candidate uses scoped Apache-2.0 through its
own `LICENSE` and `NOTICE`. The owner may change independently owned scope before
public release after authorship review; the tool also recognizes MIT for that
purpose. An inventory entry is an assertion to review, not proof of copyright
ownership. It does not itself license a file. Retain the actual applicable
license/copyright material in the source candidate before publication. Do not
relabel copied GPL or SDK code.

The directory's `LICENSE` may use origin kind `license-text` to identify a
standard license reproduced from its publisher. That classification is limited
to the file named `LICENSE`; it cannot allowlist borrowed implementation code.

```powershell
python tools/public-release/export_source.py --inventory tools/public-release/candidate-inventory.json
python tools/public-release/export_source.py --inventory tools/public-release/candidate-inventory.json --out tmp/public-source-candidate-20261018
python -m unittest discover -s tools/public-release -p 'test_*.py' -v
```

The exporter requires an exact hash, recognized explicit license and origin for
every file. New unclassified files, duplicate paths/JSON keys, traversal,
symlinks/junctions, hard links, private/generated directories, binary extensions
or binary content all reject the candidate. Tests/build outputs belong under
`tmp`, not the source scope. Output must be a fresh directory below the currently
Git-ignored repository `tmp/`; it cannot overwrite a previous export. It copies
verified bytes and writes `PUBLIC_SOURCE_CANDIDATE.json`. A failed partial output
is retained for inspection and cannot be reused.

Review the exact exported source, notices and inventory before considering a
separate public repository. The exporter does not detect every secret, prove
independent authorship or validate a combined firmware's licensing. See
[`docs/LICENSING.md`](../../docs/LICENSING.md) and the dated
[`source audit`](../../docs/PUBLIC_SOURCE_AUDIT_20261018.md).

The isolated candidate contains portable host tests, the static browser page
and the offline container tool. It is not a full target firmware distribution.
JieLi SDK inputs, surrounding USB/boot integration and their linked-image audit
dependencies are excluded. CMake reports and skips integration-only targets
whose surrounding sources are absent; Python linked-image tests similarly skip
without the full checkout and an explicitly selected built image. Those skips
are not passing integration or hardware results.
