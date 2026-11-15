"""Tests for scripts/json_merge3.py.

Run: python -m unittest discover -s tests/scripts
"""
import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "json_merge3.py"


def load():
    spec = importlib.util.spec_from_file_location("json_merge3", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Merge3(unittest.TestCase):
    """merge3(base, primary, edition) -> (merged, conflicts)."""

    def setUp(self):
        self.m = load()

    def test_each_side_changes_a_different_key(self):
        merged, conflicts = self.m.merge3(
            {"a": 1, "b": 2}, {"a": 10, "b": 2}, {"a": 1, "b": 20})
        self.assertEqual(conflicts, [])
        self.assertEqual(merged, {"a": 10, "b": 20})

    def test_same_change_on_both_sides(self):
        merged, conflicts = self.m.merge3({"a": 1}, {"a": 5}, {"a": 5})
        self.assertEqual((merged, conflicts), ({"a": 5}, []))

    def test_different_changes_to_one_key_conflict(self):
        merged, conflicts = self.m.merge3({"a": 1}, {"a": 2}, {"a": 3})
        self.assertEqual(conflicts, [["a"]])

    def test_edition_adds_a_key_after_its_neighbour(self):
        merged, conflicts = self.m.merge3(
            {"a": 1, "c": 3},
            {"a": 1, "z": 0, "c": 3},
            {"a": 1, "b": 2, "c": 3})
        self.assertEqual(conflicts, [])
        self.assertEqual(list(merged), ["a", "b", "z", "c"])

    def test_edition_removes_an_unchanged_key(self):
        merged, conflicts = self.m.merge3(
            {"a": 1, "b": 2}, {"a": 1, "b": 2}, {"a": 1})
        self.assertEqual((merged, conflicts), ({"a": 1}, []))

    def test_edition_removes_a_key_primary_changed_conflicts(self):
        merged, conflicts = self.m.merge3(
            {"a": 1, "b": 2}, {"a": 1, "b": 9}, {"a": 1})
        self.assertEqual(conflicts, [["b"]])

    def test_primary_removes_a_key_edition_left_alone(self):
        merged, conflicts = self.m.merge3(
            {"a": 1, "b": 2}, {"a": 1}, {"a": 1, "b": 2})
        self.assertEqual((merged, conflicts), ({"a": 1}, []))

    def test_nested_objects_merge_by_key(self):
        merged, conflicts = self.m.merge3(
            {"o": {"x": 1, "y": 1}},
            {"o": {"x": 2, "y": 1}},
            {"o": {"x": 1, "y": 2}})
        self.assertEqual((merged, conflicts), ({"o": {"x": 2, "y": 2}}, []))

    def test_lists_are_one_value(self):
        merged, conflicts = self.m.merge3({"l": ["1"]}, {"l": ["2"]}, {"l": ["3"]})
        self.assertEqual(conflicts, [["l"]])

    def test_key_added_on_both_sides_with_different_values_conflicts(self):
        merged, conflicts = self.m.merge3({}, {"n": 1}, {"n": 2})
        self.assertEqual(conflicts, [["n"]])


class MergeText(unittest.TestCase):
    """merge_text keeps the primary file's layout: 4-space indent, its final newline, its line endings."""

    def setUp(self):
        self.m = load()

    def test_layout_follows_primary(self):
        base = '{\n    "a": 1\n}'
        primary = '{\r\n    "a": 1,\r\n    "c": "é"\r\n}'
        edition = '{\n    "a": 2\n}\n'
        text, conflicts = self.m.merge_text(base, primary, edition)
        self.assertEqual(conflicts, [])
        self.assertEqual(text, '{\r\n    "a": 2,\r\n    "c": "é"\r\n}')

    def test_missing_base_merges_as_empty_object(self):
        text, conflicts = self.m.merge_text(None, '{\n    "a": 1\n}', '{\n    "b": 2\n}')
        self.assertEqual(conflicts, [])
        self.assertEqual(text, '{\n    "a": 1,\n    "b": 2\n}')


if __name__ == "__main__":
    unittest.main()
