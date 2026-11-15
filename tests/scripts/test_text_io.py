"""Tests for scripts/text_io.py.

Run: python -m unittest discover -s tests/scripts
"""
import importlib.util
import pathlib
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).resolve().parents[2] / "scripts"


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TextIo(unittest.TestCase):
    def setUp(self):
        self.m = load("text_io")
        self.tmp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.tmp.name) / "f.txt"

    def tearDown(self):
        self.tmp.cleanup()

    def test_read_exact_keeps_crlf(self):
        self.path.write_bytes(b"a\r\nb\r\n")
        self.assertEqual(self.m.read_exact(self.path), "a\r\nb\r\n")

    def test_write_exact_writes_lf_as_lf(self):
        self.m.write_exact(self.path, "a\nb\n")
        self.assertEqual(self.path.read_bytes(), b"a\nb\n")

    def test_match_newlines_follows_a_crlf_original(self):
        self.assertEqual(self.m.match_newlines("x\ny\n", "old\r\n"), "x\r\ny\r\n")

    def test_match_newlines_leaves_lf_original_alone(self):
        self.assertEqual(self.m.match_newlines("x\ny\n", "old\n"), "x\ny\n")


class ScriptsKeepLineEndings(unittest.TestCase):
    """Each script that rewrites tracked files in place keeps their line endings."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_recolor_keeps_lf(self):
        recolor = load("recolor_blue_steel")
        recolor.REPO = self.root
        path = self.root / "a.css"
        path.write_bytes(b"a { color: #009688; }\n")
        recolor.process_file(path, dry_run=False)
        self.assertEqual(path.read_bytes(), b"a { color: #2E86C1; }\n")

    def test_migrate_keeps_lf(self):
        migrate = load("migrate_builtin_filament_profiles")
        migrate.REPO_ROOT = self.root
        path = self.root / "p.json"
        path.write_bytes(b'{\n  "filament_type": [\n    "PLA"\n  ]\n}\n')
        migrate.migrate_file(path, dry_run=False)
        self.assertNotIn(b"\r\n", path.read_bytes())


if __name__ == "__main__":
    unittest.main()
