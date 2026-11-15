"""Tests for scripts/drop_comment_edits.py.

Run: python -m unittest discover -s tests/scripts
"""
import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "drop_comment_edits.py"


def load():
    spec = importlib.util.spec_from_file_location("drop_comment_edits", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CodeOnly(unittest.TestCase):
    """code_only_lines removes C/C++ comments, keeping string and char literals."""

    def setUp(self):
        self.m = load()

    def test_line_comment(self):
        self.assertEqual(self.m.code_only_lines(["int a; // note"]), ["int a;"])

    def test_block_comment_inside_a_line(self):
        self.assertEqual(self.m.code_only_lines(["f(/* x */ 1);"]), ["f( 1);"])

    def test_block_comment_across_lines(self):
        self.assertEqual(
            self.m.code_only_lines(["a; /* one", "two */ b;"]), ["a;", " b;"])

    def test_slashes_in_a_string_are_code(self):
        self.assertEqual(
            self.m.code_only_lines(['s = "http://x"; // c']), ['s = "http://x";'])

    def test_escaped_quote_in_a_string(self):
        self.assertEqual(
            self.m.code_only_lines([r's = "a\"//b"; // c']), [r's = "a\"//b";'])

    def test_char_literal(self):
        self.assertEqual(self.m.code_only_lines(["c = '/'; // c"]), ["c = '/';"])


class DropCommentEdits(unittest.TestCase):
    """drop_comment_edits(base, edition) undoes the edition's comment-only edits."""

    def setUp(self):
        self.m = load()

    def test_translated_comment_goes_back_to_base(self):
        base = ["int a; // 中文", "int b;"]
        edition = ["int a; // English", "int b;"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), base)

    def test_code_change_is_kept(self):
        base = ["int a = 1;"]
        edition = ["int a = 2;"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), edition)

    def test_code_and_comment_lines_in_one_block_split_per_line(self):
        base = ["// 中文", "int a = 1;"]
        edition = ["// English", "int a = 2;"]
        self.assertEqual(
            self.m.drop_comment_edits(base, edition), ["// 中文", "int a = 2;"])

    def test_code_change_on_a_line_keeps_its_new_comment(self):
        base = ["int a = 1; // 中文"]
        edition = ["int a = 2; // English"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), edition)

    def test_deleted_comment_is_restored(self):
        base = ["// 中文", "int a;"]
        edition = ["int a;"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), base)

    def test_inserted_lines_are_kept(self):
        base = ["int a;"]
        edition = ["int a;", "// our new helper", "int b;"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), edition)

    def test_translated_string_literal_is_kept(self):
        base = ['log("中文");']
        edition = ['log("English");']
        self.assertEqual(self.m.drop_comment_edits(base, edition), edition)

    def test_line_endings_are_kept_as_given(self):
        base = ["int a; // 中文\r\n"]
        edition = ["int a; // English\r\n"]
        self.assertEqual(self.m.drop_comment_edits(base, edition), base)


if __name__ == "__main__":
    unittest.main()
