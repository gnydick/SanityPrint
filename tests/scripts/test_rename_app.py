"""Tests for scripts/rename_app.py.

Run: python -m unittest discover -s tests/scripts
"""
import importlib.util
import pathlib
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "rename_app.py"


def load_rename_app():
    spec = importlib.util.spec_from_file_location("rename_app", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProcessFileKeepsLineEndings(unittest.TestCase):
    """process_file must change only the app name, never the line endings."""

    def setUp(self):
        self.rename_app = load_rename_app()
        self.tmp = tempfile.TemporaryDirectory()
        self.rename_app.REPO = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_on(self, content: bytes) -> bytes:
        path = pathlib.Path(self.tmp.name) / "sample.cpp"
        path.write_bytes(content)
        self.rename_app.process_file(path, dry_run=False)
        return path.read_bytes()

    def test_lf_file_stays_lf(self):
        result = self.run_on(b"// CrealityPrint\nint x;\n")
        self.assertEqual(result, b"// SanityPrint\nint x;\n")

    def test_crlf_file_stays_crlf(self):
        result = self.run_on(b"// CrealityPrint\r\nint x;\r\n")
        self.assertEqual(result, b"// SanityPrint\r\nint x;\r\n")


if __name__ == "__main__":
    unittest.main()
