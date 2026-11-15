#!/usr/bin/env python3
"""
Bulk color swap: Creality green → blue-steel.
Run from repo root: python scripts/recolor_blue_steel.py [--dry-run]

Blue-steel palette (replaces Creality teal/green brand colors):
  Primary   #009688 / rgb(0,150,136)   → #2E86C1 / rgb(46,134,193)
  Hover     #26A69A / rgb(38,166,154)  → #5DADE2 / rgb(93,173,226)
  Pressed   #00897B / rgb(0,137,123)   → #1A6FA3 / rgb(26,111,163)
  Accent    #17CC5F / #15BF59 /
            #15C059 / #1FCA63          → #3498DB
  Dark-mode #00675b                    → #1A5F8A
"""
import argparse, pathlib, sys

REPO = pathlib.Path(__file__).parent.parent

SKIP_DIRS = {".git", "scripts"}
SKIP_SUFFIXES = {".png", ".ico", ".icns", ".jpg", ".jpeg", ".gif",
                 ".ttf", ".otf", ".woff", ".woff2", ".dll", ".lib",
                 ".exe", ".obj", ".pdb", ".mo"}

# Ordered: longest/most-specific first to avoid partial hex collisions.
# Each entry: (old_string, new_string)  — case-sensitive, exact match.
REPLACEMENTS = [
    # --- Hex with 0x prefix (C++) ---
    ("0x17CC5F",        "0x3498DB"),
    ("0x15BF59",        "0x2E86C1"),
    ("0x15C059",        "0x3498DB"),
    ("0x1FCA63",        "0x3498DB"),
    ("0x009688",        "0x2E86C1"),
    ("0x26A69A",        "0x5DADE2"),
    ("0x00675b",        "0x1A5F8A"),
    ("0x00675B",        "0x1A5F8A"),

    # --- CSS/SVG/HTML hex (# prefix, uppercase) ---
    ("#17CC5F",         "#3498DB"),
    ("#15BF59",         "#2E86C1"),
    ("#15C059",         "#3498DB"),
    ("#1FCA63",         "#3498DB"),
    ("#009688",         "#2E86C1"),
    ("#26A69A",         "#5DADE2"),
    ("#00897B",         "#1A6FA3"),
    ("#00675B",         "#1A5F8A"),
    ("#00675b",         "#1A5F8A"),

    # --- Lowercase hex variants (CSS sometimes lowercases) ---
    ("#17cc5f",         "#3498db"),
    ("#15bf59",         "#2e86c1"),
    ("#15c059",         "#3498db"),
    ("#1fca63",         "#3498db"),
    ("#009688",         "#2e86c1"),   # already covered above, harmless
    ("#26a69a",         "#5dade2"),
    ("#00897b",         "#1a6fa3"),

    # --- wxColour / RGB tuples (C++) ---
    # These must be matched exactly including spacing as they appear in source.
    ("wxColour(0, 150, 136)",          "wxColour(46, 134, 193)"),
    ("wxColour(38, 166, 154)",         "wxColour(93, 173, 226)"),
    ("wxColour(0, 137, 123)",          "wxColour(26, 111, 163)"),
    ("wxColour(0, 107, 91)",           "wxColour(26, 95, 138)"),
    # Without spaces
    ("wxColour(0,150,136)",            "wxColour(46,134,193)"),
    ("wxColour(38,166,154)",           "wxColour(93,173,226)"),
    ("wxColour(0,137,123)",            "wxColour(26,111,163)"),

    # --- Plain RGB tuples that appear as constructor args in wxColour(r,g,b) ---
    # Catch any remaining raw forms used in e.g. StateColor entries
    ("0, 150, 136",    "46, 134, 193"),
    ("38, 166, 154",   "93, 173, 226"),
    ("0, 137, 123",    "26, 111, 163"),
    ("0, 107, 91",     "26, 95, 138"),
    ("21, 191, 89",    "52, 152, 219"),   # #15BF59 as rgb
    ("21, 192, 89",    "52, 152, 219"),   # #15C059 as rgb
    ("23, 204, 95",    "52, 152, 219"),   # #17CC5F as rgb
    ("31, 202, 99",    "65, 179, 224"),   # #1FCA63 as rgb

    # --- CSS rgb() functions ---
    ("rgb(0, 150, 136)",    "rgb(46, 134, 193)"),
    ("rgb(38, 166, 154)",   "rgb(93, 173, 226)"),
    ("rgb(0, 137, 123)",    "rgb(26, 111, 163)"),

    # --- SVG fill attributes with bare hex (no #) —rare but possible ---
    # skip — covered by # forms above
]


def should_skip(path: pathlib.Path) -> bool:
    rel = path.relative_to(REPO).as_posix()
    if path.suffix.lower() in SKIP_SUFFIXES:
        return True
    for sd in SKIP_DIRS:
        if rel.startswith(sd):
            return True
    return False


def process_file(path: pathlib.Path, dry_run: bool) -> int:
    try:
        original = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return 0
    text = original
    count = 0
    for old, new in REPLACEMENTS:
        n = text.count(old)
        if n:
            text = text.replace(old, new)
            count += n
    if count == 0:
        return 0
    rel = path.relative_to(REPO)
    print(f"  {'DRY ' if dry_run else ''}RECOLOR {rel}  ({count} swaps)")
    if not dry_run:
        path.write_text(text, encoding="utf-8")
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    total_files = total_swaps = 0
    for path in sorted(REPO.rglob("*")):
        if path.is_dir() or should_skip(path):
            continue
        n = process_file(path, args.dry_run)
        if n:
            total_files += 1
            total_swaps += n

    print(f"\n{'[DRY RUN] ' if args.dry_run else ''}Done: {total_swaps} color swaps across {total_files} files.")


if __name__ == "__main__":
    main()
