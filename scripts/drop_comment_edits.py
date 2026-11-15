#!/usr/bin/env python3
"""Undo an edition's comment-only edits to a C/C++ file.

Compares the edition with the base line by line. Where a changed or
deleted block differs from the base only in comments, the base lines come
back; real code edits, string literal edits and inserted lines stay. A
changed block with the same line count is judged line by line, so a code
edit next to a translated comment keeps the code edit and the base comment.

Used before a 3-way merge with upstream, so the merge keeps upstream's
comments and carries only the edition's code edits.

Usage:
  python scripts/drop_comment_edits.py <base> <edition> -o <out>
"""
import argparse
import difflib
import sys


def code_only_lines(lines):
    """Return each line with C/C++ comments removed and trailing space stripped."""
    out = []
    in_block = False
    for line in lines:
        text = line.rstrip("\r\n")
        kept = []
        i, n = 0, len(text)
        quote = None
        while i < n:
            c = text[i]
            if in_block:
                if text.startswith("*/", i):
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            if quote:
                kept.append(c)
                if c == "\\" and i + 1 < n:
                    kept.append(text[i + 1])
                    i += 2
                    continue
                if c == quote:
                    quote = None
                i += 1
                continue
            if text.startswith("//", i):
                break
            if text.startswith("/*", i):
                in_block = True
                i += 2
                continue
            if c in "\"'":
                quote = c
            kept.append(c)
            i += 1
        out.append("".join(kept).rstrip())
    return out


def drop_comment_edits(base, edition):
    """Return edition with every comment-only edit against base undone."""
    base_code = code_only_lines(base)
    edition_code = code_only_lines(edition)
    matcher = difflib.SequenceMatcher(None, base, edition, autojunk=False)
    result = []
    for tag, b0, b1, e0, e1 in matcher.get_opcodes():
        if tag == "equal" or tag == "insert":
            result.extend(edition[e0:e1])
        elif tag == "delete":
            if all(code == "" for code in base_code[b0:b1]):
                result.extend(base[b0:b1])       # only comments or blanks were deleted
        elif base_code[b0:b1] == edition_code[e0:e1]:
            result.extend(base[b0:b1])
        elif b1 - b0 == e1 - e0:
            for k in range(b1 - b0):
                same = base_code[b0 + k] == edition_code[e0 + k]
                result.append(base[b0 + k] if same else edition[e0 + k])
        else:
            result.extend(edition[e0:e1])
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("base")
    parser.add_argument("edition")
    parser.add_argument("-o", "--out", required=True)
    args = parser.parse_args(argv)
    read = lambda p: open(p, encoding="utf-8", errors="surrogateescape", newline="").read().splitlines(keepends=True)
    lines = drop_comment_edits(read(args.base), read(args.edition))
    with open(args.out, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write("".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
