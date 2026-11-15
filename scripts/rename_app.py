#!/usr/bin/env python3
"""
Bulk rename: CrealityPrint → SanityPrint throughout the source tree.
Run from repo root: python scripts/rename_app.py [--dry-run]
"""
import argparse, pathlib, re, sys

REPO = pathlib.Path(__file__).parent.parent

# Directories/files to skip entirely
SKIP_DIRS = {
    "resources/profiles/Creality",   # vendor hardware profiles — not the app name
    ".git",
    "scripts",                        # don't rewrite ourselves mid-run
}
SKIP_FILES = {
    "src/libslic3r/creality.cmake",   # internal build plumbing name, not user-visible
}
SKIP_SUFFIXES = {".png", ".ico", ".icns", ".jpg", ".jpeg", ".gif",
                 ".ttf", ".otf", ".woff", ".woff2", ".dll", ".lib",
                 ".exe", ".obj", ".pdb", ".mo"}

# Ordered replacements — longer/more-specific patterns first to avoid partial matches
REPLACEMENTS = [
    # CMake variable names
    ("CREALITYPRINT_VERSION",          "SANITYPRINT_VERSION"),
    ("CrealityPrintLink",              "SanityPrintLink"),
    # Folder/path strings
    ("Creality Print",                 "Sanity Print"),
    ("creality_print",                 "sanity_print"),
    ("crealityprint",                  "sanityprint"),
    # Class/symbol names in source — keep these after the above
    ("CrealityPrintTaskBarIcon",       "SanityPrintTaskBarIcon"),
    # Main name (case variants) — broadest last
    ("CREALITYPRINT",                  "SANITYPRINT"),
    ("CrealityPrint",                  "SanityPrint"),
    ("Creality-Print",                 "Sanity-Print"),
    # APP_KEY / APP_USE_FOLDER (set in version.inc)
    # handled by the CrealityPrint → SanityPrint pass above since they contain it
]

# Also rename these files themselves
FILE_RENAMES = {
    "src/platform/msw/CrealityPrint.rc.in":
        "src/platform/msw/SanityPrint.rc.in",
    "src/platform/msw/CrealityPrint-gcodeviewer.rc.in":
        "src/platform/msw/SanityPrint-gcodeviewer.rc.in",
    "src/platform/unix/CrealityPrint.desktop":
        "src/platform/unix/SanityPrint.desktop",
    ".run/CrealityPrint.run.xml":
        ".run/SanityPrint.run.xml",
    "flatpak/io.github.crealityofficial.CrealityPrint.yml":
        "flatpak/io.github.gnydick.SanityPrint.yml",
    "flatpak/io.github.crealityofficial.CrealityPrint.metainfo.xml":
        "flatpak/io.github.gnydick.SanityPrint.metainfo.xml",
}


def should_skip(path: pathlib.Path) -> bool:
    rel = path.relative_to(REPO).as_posix()
    if path.suffix.lower() in SKIP_SUFFIXES:
        return True
    for sd in SKIP_DIRS:
        if rel.startswith(sd):
            return True
    if rel in SKIP_FILES:
        return True
    return False


def replace_in_text(text: str) -> tuple[str, int]:
    count = 0
    for old, new in REPLACEMENTS:
        n = text.count(old)
        if n:
            text = text.replace(old, new)
            count += n
    return text, count


def process_file(path: pathlib.Path, dry_run: bool) -> int:
    try:
        original = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return 0
    new_text, n = replace_in_text(original)
    if n == 0:
        return 0
    rel = path.relative_to(REPO)
    print(f"  {'DRY ' if dry_run else ''}EDIT {rel}  ({n} replacements)")
    if not dry_run:
        path.write_text(new_text, encoding="utf-8")
    return n


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    total_files = total_replacements = 0

    # Walk all text files
    for path in sorted(REPO.rglob("*")):
        if path.is_dir() or should_skip(path):
            continue
        n = process_file(path, args.dry_run)
        if n:
            total_files += 1
            total_replacements += n

    # Rename files
    for src_rel, dst_rel in FILE_RENAMES.items():
        src = REPO / src_rel
        dst = REPO / dst_rel
        if src.exists():
            print(f"  {'DRY ' if args.dry_run else ''}RENAME {src_rel} -> {dst_rel}")
            if not args.dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                src.rename(dst)

    print(f"\n{'[DRY RUN] ' if args.dry_run else ''}Done: {total_replacements} replacements across {total_files} files.")


if __name__ == "__main__":
    main()
