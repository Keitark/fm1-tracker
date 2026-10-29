"""Host-side catalog and edit-preserving selection checks."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE))
import library_server as server


class LibraryServerTests(unittest.TestCase):
    def test_physical_save_exports_then_acknowledges_exact_request(self):
        with tempfile.TemporaryDirectory() as root:
            with patch.object(server.client, "save", return_value=(2108, 0x12345678)) as save, \
                 patch.object(server.client, "exchange", return_value="OK LIB DONE") as exchange:
                self.assertIsNone(server.handle_pick(None, [], "LIB SAVE 002A", Path(root)))
            destination = save.call_args.args[1]
            self.assertEqual(destination.parent, Path(root))
            self.assertTrue(destination.name.startswith("fm1-song-"))
            self.assertEqual(destination.suffix, ".mod")
            exchange.assert_called_once_with(None, "LIB DONE 002A")

    def test_physical_save_failure_is_reported_without_done(self):
        with tempfile.TemporaryDirectory() as root:
            with patch.object(server.client, "save", side_effect=OSError("disk full")), \
                 patch.object(server.client, "exchange", return_value="OK LIB FAIL") as exchange:
                with self.assertRaisesRegex(OSError, "disk full"):
                    server.handle_pick(None, [], "LIB SAVE 002B", Path(root))
            exchange.assert_called_once_with(None, "LIB FAIL 002B")

    def test_discovery_rejects_oversize_and_keeps_supported_mod(self):
        data = bytearray(2108)
        data[950] = 1
        data[1080:1084] = b"M.K."
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            (directory / "ok.mod").write_bytes(data)
            (directory / "too_large.mod").write_bytes(bytes(server.client.LIMIT + 1))
            with self.assertRaisesRegex(ValueError, "unsupported MOD"):
                server.discover([directory])
            (directory / "too_large.mod").unlink()
            songs = server.discover([directory])
            self.assertEqual([(s.name, s.size) for s in songs], [("OK", 2108)])

    def test_catalog_encodes_titles_and_lengths(self):
        song = server.Song(Path("stateart.mod"), "STATE OF THE ART", 230652)
        with patch.object(server.client, "exchange", side_effect=["OK LIB RESET", "OK LIB ADD", "OK LIB READY"]) as exchange:
            server.send_catalog(None, [song])
        self.assertEqual(exchange.call_args_list[1].args[1],
                         "LIB ADD 000384FC " + "STATE OF THE ART".ljust(20).encode("ascii").hex())

    def test_dirty_song_is_saved_before_selected_file_load(self):
        song = server.Song(Path("stateart.mod"), "STATE OF THE ART", 230652)
        with tempfile.TemporaryDirectory() as root:
            with patch.object(server.client, "exchange", side_effect=["MOD status ready=1 running=0 dirty=1", "OK LIB DONE"]) as exchange, \
                 patch.object(server.client, "save") as save, \
                 patch.object(server.client, "upload", return_value=(230652, 123)) as upload:
                result = server.handle_pick(None, [song], "LIB PICK 0007 00", Path(root))
            self.assertEqual(result, song)
            self.assertEqual(save.call_count, 1)
            self.assertEqual(upload.call_args.args[1], song.path)
            self.assertEqual(exchange.call_args_list[-1].args[1], "LIB DONE 0007")

    def test_failed_save_prevents_song_replacement(self):
        song = server.Song(Path("stateart.mod"), "STATE OF THE ART", 230652)
        with tempfile.TemporaryDirectory() as root:
            with patch.object(server.client, "exchange", side_effect=["MOD status ready=1 running=0 dirty=1", "OK LIB FAIL"]), \
                 patch.object(server.client, "save", side_effect=OSError("disk full")), \
                 patch.object(server.client, "upload") as upload:
                with self.assertRaisesRegex(OSError, "disk full"):
                    server.handle_pick(None, [song], "LIB PICK 0007 00", Path(root))
                upload.assert_not_called()


if __name__ == "__main__":
    unittest.main()
