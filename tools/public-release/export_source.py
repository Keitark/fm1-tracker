#!/usr/bin/env python3
"""Audit and copy an explicitly inventoried, unreviewed source candidate.

No Git history, device access, dependency fetching, firmware building or publishing.
This tool checks a narrow mechanical boundary, not authorship or legal permission.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys

SOURCE_ROOTS = ("firmware/usb-midi",)
LICENSES = {"Apache-2.0", "MIT"}
TEXT_SUFFIXES = {".c", ".h", ".md", ".txt", ".json", ".py", ".mjs", ".js", ".html", ".css", ".cmake"}
TEXT_NAMES = {"LICENSE", "NOTICE", "CMakeLists.txt", "Makefile"}
DENIED_PARTS = {
    "build", "dist", "vendor", "sdk", "references", "node_modules", "tmp",
    "private-backups", "private-library", "private-media", "__pycache__",
}
MAX_FILE_BYTES = 2 * 1024 * 1024
MAX_INVENTORY_BYTES = 1024 * 1024
HEX256 = re.compile(r"[0-9a-f]{64}\Z")
SAFE_PATH = re.compile(r"[A-Za-z0-9_./-]+\Z")


class AuditError(ValueError):
    """Candidate is outside the explicitly approved mechanical boundary."""


def relative_source_path(value: object) -> str:
    if not isinstance(value, str) or not SAFE_PATH.fullmatch(value):
        raise AuditError("Source path must use safe relative POSIX characters")
    parts = value.split("/")
    if any(p in {"", ".", ".."} for p in parts) or PurePosixPath(value).is_absolute():
        raise AuditError("Traversal, absolute and noncanonical paths are forbidden")
    if any(p.startswith(".") or p.casefold() in DENIED_PARTS or p.casefold().startswith("private") for p in parts):
        raise AuditError("Private, hidden, dependency and generated paths are forbidden")
    if not any(value.startswith(scope + "/") for scope in SOURCE_ROOTS):
        raise AuditError("Source path is outside the reviewed export scope")
    if PurePosixPath(value).suffix.casefold() not in TEXT_SUFFIXES and PurePosixPath(value).name not in TEXT_NAMES:
        raise AuditError("Binary or unrecognized source extension is forbidden")
    return value


def reject_link(path: Path) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as exc:
        raise AuditError(f"Cannot inspect candidate path: {path.name}") from exc
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise AuditError("Symlinks, junctions and reparse points are forbidden")
    return info


def check_chain(repo: Path, relative: str) -> Path:
    current = repo
    reject_link(current)
    for part in relative.split("/"):
        current = current / part
        reject_link(current)
    if not current.resolve().is_relative_to(repo.resolve()):
        raise AuditError("Resolved source path escaped the repository")
    return current


def read_text_source(path: Path) -> bytes:
    info = reject_link(path)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise AuditError("Only regular source files with no hard links are permitted")
    if info.st_size > MAX_FILE_BYTES:
        raise AuditError("Candidate source file exceeds the size limit")
    try:
        with path.open("rb") as handle:
            content = handle.read(MAX_FILE_BYTES + 1)
        decoded = content.decode("utf-8")
    except (OSError, UnicodeError) as exc:
        raise AuditError("Candidate source must be readable UTF-8 text") from exc
    if len(content) > MAX_FILE_BYTES or any((ord(c) < 32 and c not in "\r\n\t\f") or ord(c) == 127 for c in decoded):
        raise AuditError("Binary content or control bytes are forbidden")
    return content


def load_inventory(path: Path) -> dict:
    reject_link(path)
    if path.stat().st_size > MAX_INVENTORY_BYTES:
        raise AuditError("Inventory exceeds the size limit")
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise AuditError("Inventory must be a UTF-8 JSON object") from exc


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise AuditError("Duplicate JSON keys are forbidden")
        result[key] = value
    return result


def audit_source(repo: Path, inventory: dict) -> list[tuple[dict, bytes]]:
    if not isinstance(inventory, dict) or set(inventory) != {"schema_version", "status", "source_roots", "files"}:
        raise AuditError("Unexpected or missing inventory fields")
    if type(inventory["schema_version"]) is not int or inventory["schema_version"] != 1:
        raise AuditError("Unsupported inventory schema")
    if inventory["status"] != "candidate-unreviewed" or inventory["source_roots"] != list(SOURCE_ROOTS):
        raise AuditError("Inventory must retain the unreviewed status and exact source scope")
    entries = inventory["files"]
    if not isinstance(entries, list) or not entries:
        raise AuditError("Inventory needs an explicit nonempty file list")
    audited, seen = [], set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256", "license", "origin"}:
            raise AuditError("Every file needs exact path, SHA256, license and origin")
        relative = relative_source_path(entry["path"])
        folded = relative.casefold()
        if folded in seen:
            raise AuditError("Duplicate or case-colliding source paths are forbidden")
        seen.add(folded)
        if not isinstance(entry["sha256"], str) or not HEX256.fullmatch(entry["sha256"]):
            raise AuditError("Every SHA256 must be 64 lowercase hexadecimal characters")
        if not isinstance(entry["license"], str) or entry["license"] not in LICENSES:
            raise AuditError("Only explicitly selected independent MIT/Apache-2.0 sources are in this initial scope")
        origin = entry["origin"]
        if not isinstance(origin, dict) or set(origin) != {"kind", "description"}:
            raise AuditError("Initial source scope permits independently authored origins only")
        if origin["kind"] != "independently-authored":
            if origin["kind"] != "license-text" or PurePosixPath(relative).name != "LICENSE":
                raise AuditError("Only the scoped LICENSE may use a standard license-text origin")
        if not isinstance(origin["description"], str) or not 10 <= len(origin["description"]) <= 1000 or any(ord(c) < 32 for c in origin["description"]):
            raise AuditError("Origin needs a concise printable authorship/provenance description")
        content = read_text_source(check_chain(repo, relative))
        if hashlib.sha256(content).hexdigest() != entry["sha256"]:
            raise AuditError(f"SHA256 mismatch: {relative}")
        audited.append((entry, content))
    discovered = set()
    def walk_error(error: OSError) -> None:
        raise AuditError("Cannot walk the complete source scope") from error
    for scope in SOURCE_ROOTS:
        scope_path = check_chain(repo, scope)
        for base, directories, names in os.walk(scope_path, followlinks=False, onerror=walk_error):
            for name in directories + names:
                item = Path(base) / name
                info = reject_link(item)
                relative = item.relative_to(repo).as_posix()
                if stat.S_ISDIR(info.st_mode):
                    if name.startswith(".") or name.casefold() in DENIED_PARTS or name.casefold().startswith("private"):
                        raise AuditError("Private/generated/dependency directory in export scope")
                else:
                    relative_source_path(relative)
                    discovered.add(relative.casefold())
    if discovered != seen:
        missing = sorted(discovered - seen)
        raise AuditError("Unclassified or missing source files in export scope" + (": " + ", ".join(missing[:5]) if missing else ""))
    return sorted(audited, key=lambda item: item[0]["path"])


def ignored_by_git(repo: Path, relative: str) -> bool:
    try:
        check = subprocess.run(["git", "-C", str(repo), "check-ignore", "--quiet", "--no-index", "--", relative], capture_output=True, check=False)
    except OSError as exc:
        raise AuditError("Git ignore verification is unavailable") from exc
    return check.returncode == 0


def validate_output(repo: Path, output: Path) -> Path:
    # Reject lexical traversal before resolving paths; no reuse or overwrite.
    if ".." in output.parts:
        raise AuditError("Output traversal is forbidden")
    output = output.absolute()
    repo = repo.absolute()
    try:
        relative = output.relative_to(repo)
    except ValueError as exc:
        raise AuditError("Output must be inside this repository's ignored tmp directory") from exc
    if len(relative.parts) < 2 or relative.parts[0] != "tmp":
        raise AuditError("Output must be a fresh directory strictly below tmp")
    current = repo
    reject_link(current)
    for part in relative.parts:
        current = current / part
        if current.exists() or current.is_symlink():
            reject_link(current)
    if output.exists() or output.is_symlink():
        raise AuditError("Output already exists; candidate export never overwrites")
    if not ignored_by_git(repo, relative.as_posix() + "/PUBLIC_SOURCE_CANDIDATE.json"):
        raise AuditError("Output is not ignored by the repository's current Git rules")
    if not output.resolve().is_relative_to((repo / "tmp").resolve()):
        raise AuditError("Resolved output escaped ignored tmp")
    return output


def export_source(repo: Path, inventory: dict, output: Path) -> dict:
    audited = audit_source(repo, inventory)
    output = validate_output(repo, output)
    # Bytes were read and verified during audit; copy that snapshot, not later reads.
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "schema_version": 1,
        "status": "candidate-unreviewed",
        "warning": "NOT APPROVED FOR PUBLICATION. Manual source, authorship, license and sensitive-data review remains required. No firmware or hardware acceptance is implied.",
        "source_roots": list(SOURCE_ROOTS),
        "files": [entry for entry, _ in audited],
        "generated_metadata": ["PUBLIC_SOURCE_CANDIDATE.json"],
        "git_history_included": False,
        "external_dependencies_not_included": [
            "JieLi SDK source, libraries and toolchain",
            "Surrounding firmware/usb-diag and firmware/nes integration/build/audit source",
            "Official OTA resource and all unit-private firmware/baseline packages",
        ],
    }
    # A failed partial output is intentionally left for inspection, never reused.
    for entry, content in audited:
        destination = output.joinpath(*entry["path"].split("/"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as handle:
            handle.write(content)
    with (output / "PUBLIC_SOURCE_CANDIDATE.json").open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=True)
        handle.write("\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--out", type=Path, help="Optional fresh directory under this repository's ignored tmp; omit for audit only")
    args = parser.parse_args()
    repo = Path(__file__).absolute().parents[2]
    try:
        inventory = load_inventory(args.inventory)
        if args.out:
            report = export_source(repo, inventory, args.out)
            print(json.dumps({"status": report["status"], "files": len(report["files"]), "output": str(args.out.absolute())}))
        else:
            audited = audit_source(repo, inventory)
            print(json.dumps({"status": "candidate-unreviewed", "files": len(audited), "exported": False}))
        return 0
    except (AuditError, OSError) as exc:
        print(f"Source candidate rejected: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
