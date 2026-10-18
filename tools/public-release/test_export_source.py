"""Regression checks for the boundary that protects the private workspace."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("public_source_export", Path(__file__).with_name("export_source.py"))
export = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(export)


class ExportBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.scope = self.repo / "firmware/usb-midi"
        self.scope.mkdir(parents=True)
        self.content = b"/* Independent protocol framing test source. */\n"
        self.source = self.scope / "transport.c"
        self.source.write_bytes(self.content)
        self.inventory = {
            "schema_version": 1,
            "status": "candidate-unreviewed",
            "source_roots": ["firmware/usb-midi"],
            "files": [{
                "path": "firmware/usb-midi/transport.c",
                "sha256": hashlib.sha256(self.content).hexdigest(),
                "license": "Apache-2.0",
                "origin": {"kind": "independently-authored", "description": "Project-authored source for this boundary test."},
            }],
        }

    def assert_rejected(self, message=None):
        with self.assertRaisesRegex(export.AuditError, message or "."):
            export.audit_source(self.repo, self.inventory)

    def test_exact_inventory_exports_only_verified_source_and_candidate_receipt(self):
        # Unrelated private content and Git history are never traversed/copied.
        (self.repo / ".git").mkdir()
        (self.repo / ".git/history").write_text("PRIVATE", encoding="utf-8")
        (self.repo / "private-backups").mkdir()
        (self.repo / "private-backups/device.bin").write_bytes(b"PRIVATE")
        output = self.repo / "tmp/candidate"
        with patch.object(export, "ignored_by_git", return_value=True):
            report = export.export_source(self.repo, self.inventory, output)
        self.assertEqual((output / "firmware/usb-midi/transport.c").read_bytes(), self.content)
        self.assertEqual(sorted(p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()), ["PUBLIC_SOURCE_CANDIDATE.json", "firmware/usb-midi/transport.c"])
        self.assertEqual(report["status"], "candidate-unreviewed")
        self.assertFalse(report["git_history_included"])
        self.assertEqual(json.loads((output / "PUBLIC_SOURCE_CANDIDATE.json").read_text())["files"], self.inventory["files"])

    def test_traversal_absolute_windows_and_noncanonical_paths_rejected(self):
        for path in ("../private.c", "/firmware/usb-midi/transport.c", "C:/private.c", "firmware/usb-midi/../transport.c", "firmware\\usb-midi\\transport.c", "firmware//usb-midi/transport.c", "firmware/usb-midi/./transport.c"):
            with self.subTest(path=path):
                self.inventory["files"][0]["path"] = path
                self.assert_rejected()

    def test_new_unclassified_source_blocks_export(self):
        (self.scope / "unreviewed.c").write_text("int not_reviewed;", encoding="utf-8")
        self.assert_rejected("Unclassified")

    def test_unreadable_directory_cannot_silently_hide_unclassified_source(self):
        def incomplete_walk(path, *, followlinks, onerror):
            onerror(PermissionError("Source directory is unreadable"))
            return iter(())
        with patch.object(export.os, "walk", side_effect=incomplete_walk):
            self.assert_rejected("Cannot walk")

    def test_exact_hash_mismatch_blocks_changed_source(self):
        self.source.write_bytes(self.content + b"int changed;\n")
        self.assert_rejected("SHA256 mismatch")

    def test_private_and_binary_paths_cannot_be_inventory_allowlisted(self):
        for path in ("private-backups/stock.c", "firmware/usb-midi/private/device.c", "firmware/usb-midi/stock.fwsc", "firmware/usb-midi/image.bin", "firmware/usb-midi/vendor/copy.c", "firmware/usb-midi/.env"):
            with self.subTest(path=path):
                self.inventory["files"][0]["path"] = path
                self.assert_rejected()

    def test_disguised_binary_text_rejected(self):
        content = b"looks like C\x00but is binary"
        self.source.write_bytes(content)
        self.inventory["files"][0]["sha256"] = hashlib.sha256(content).hexdigest()
        self.assert_rejected("Binary")

    def test_unknown_or_copied_origin_and_license_rejected(self):
        for origin in ({"kind": "upstream-copy", "description": "A borrowed upstream implementation."}, {"kind": "independently-authored"}):
            with self.subTest(origin=origin):
                self.inventory["files"][0]["origin"] = origin
                self.assert_rejected("origin")
        self.inventory["files"][0]["origin"] = {"kind": "independently-authored", "description": "Project-authored boundary test."}
        for license_id in ("GPL-3.0-only", "unknown", {"license": "MIT"}):
            self.inventory["files"][0]["license"] = license_id
            self.assert_rejected("independent")

    def test_standard_license_text_origin_is_limited_to_license_file(self):
        origin = {"kind": "license-text", "description": "Apache-2.0 standard license text from the Apache Software Foundation."}
        self.inventory["files"][0]["origin"] = origin
        self.assert_rejected("Only the scoped LICENSE")
        license_path = self.scope / "LICENSE"
        license_path.write_bytes(self.content)
        self.source.unlink()
        self.inventory["files"][0]["path"] = "firmware/usb-midi/LICENSE"
        self.assertEqual(len(export.audit_source(self.repo, self.inventory)), 1)

    def test_case_colliding_duplicate_inventory_rejected(self):
        entry = dict(self.inventory["files"][0])
        entry["path"] = "firmware/usb-midi/TRANSPORT.c"
        self.inventory["files"].append(entry)
        self.assert_rejected("Duplicate")

    def test_symlink_source_cannot_escape_or_enter_candidate(self):
        target = self.repo / "outside.c"
        target.write_bytes(self.content)
        self.source.unlink()
        try:
            self.source.symlink_to(target)
        except OSError:
            self.skipTest("Host does not grant symlink creation")
        self.assert_rejected("Symlinks")

    def test_directory_symlink_blocks_even_unclassified_entry(self):
        target = self.repo / "outside"
        target.mkdir()
        try:
            (self.scope / "link").symlink_to(target, target_is_directory=True)
        except OSError:
            self.skipTest("Host does not grant symlink creation")
        self.assert_rejected("Symlinks")

    def test_windows_junction_reparse_flag_rejected(self):
        class Reparse:
            st_mode = 0o040755
            st_file_attributes = 0x400
        with patch.object(Path, "lstat", return_value=Reparse()):
            with self.assertRaisesRegex(export.AuditError, "reparse"):
                export.reject_link(self.scope)

    def test_symlink_mode_rejected_without_host_creation_permission(self):
        class Symlink:
            st_mode = 0o120777
            st_file_attributes = 0
        with patch.object(Path, "lstat", return_value=Symlink()):
            with self.assertRaisesRegex(export.AuditError, "Symlinks"):
                export.reject_link(self.source)

    def test_hard_link_count_rejected_without_host_creation_permission(self):
        class HardLink:
            st_mode = 0o100644
            st_nlink = 2
            st_size = 12
        with patch.object(export, "reject_link", return_value=HardLink()):
            with self.assertRaisesRegex(export.AuditError, "hard links"):
                export.read_text_source(self.source)

    def test_hard_link_rejected(self):
        try:
            os.link(self.source, self.repo / "private-alias.c")
        except OSError:
            self.skipTest("Host does not support hard links")
        self.assert_rejected("hard links")

    def test_generated_directory_blocks_candidate(self):
        (self.scope / "build").mkdir()
        self.assert_rejected("generated")

    def test_output_must_be_fresh_ignored_and_strictly_under_tmp(self):
        for output in (self.repo, self.repo / "tmp", self.repo / "public", self.repo / "tmp/../public", self.repo.parent / "outside-candidate"):
            with self.subTest(output=output), patch.object(export, "ignored_by_git", return_value=True):
                with self.assertRaises(export.AuditError):
                    export.validate_output(self.repo, output)
        with patch.object(export, "ignored_by_git", return_value=False):
            with self.assertRaisesRegex(export.AuditError, "not ignored"):
                export.validate_output(self.repo, self.repo / "tmp/new")
        existing = self.repo / "tmp/existing"
        existing.mkdir(parents=True)
        with patch.object(export, "ignored_by_git", return_value=True):
            with self.assertRaisesRegex(export.AuditError, "already exists"):
                export.validate_output(self.repo, existing)

    def test_output_parent_symlink_cannot_escape_tmp(self):
        (self.repo / "tmp").mkdir()
        outside = self.repo / "outside"
        outside.mkdir()
        try:
            (self.repo / "tmp/link").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("Host does not grant symlink creation")
        with patch.object(export, "ignored_by_git", return_value=True):
            with self.assertRaisesRegex(export.AuditError, "Symlinks"):
                export.validate_output(self.repo, self.repo / "tmp/link/candidate")

    def test_duplicate_json_keys_do_not_override_inventory(self):
        manifest = self.repo / "inventory.json"
        manifest.write_text('{"files": [], "files": [{"path": "private.c"}]}', encoding="utf-8")
        with self.assertRaisesRegex(export.AuditError, "Duplicate JSON"):
            export.load_inventory(manifest)

    def test_empty_or_approved_inventory_does_not_silently_pass(self):
        self.inventory["status"] = "approved"
        self.assert_rejected("unreviewed")
        self.inventory["status"] = "candidate-unreviewed"
        self.inventory["files"] = []
        self.assert_rejected("nonempty")


if __name__ == "__main__":
    unittest.main()
