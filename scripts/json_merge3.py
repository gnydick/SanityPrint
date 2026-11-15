#!/usr/bin/env python3
"""Key-level 3-way merge of JSON files.

Merges the edition's edits (base -> edition) onto the primary version
(base -> primary), key by key, so two sides that edit different keys of the
same object do not conflict. Lists and scalars are single values.

The output keeps the primary file's layout: 4-space indent, its final
newline (or none) and its line endings. Key order follows the primary file;
a key only the edition added goes right after its neighbour in the edition,
or at the end when it has no earlier neighbour.

Usage:
  python scripts/json_merge3.py <base> <primary> <edition> -o <out>
  A missing <base> path ("-") means the file is new on both sides.
Exit code: 0 merged and written; 1 conflicts (nothing written, key paths
printed); 2 bad input.
"""
import argparse
import json
import sys

_MISSING = object()


def merge3(base, primary, edition, path=None):
    """Return (merged, conflicts); each conflict is a list of keys."""
    path = path or []
    if primary == edition:
        return primary, []
    if edition == base:
        return primary, []
    if primary == base:
        return edition, []
    if not all(isinstance(x, dict) for x in (primary, edition)):
        return primary, [path]
    base = base if isinstance(base, dict) else {}
    merged, conflicts = {}, []
    for key, p in primary.items():
        b = base.get(key, _MISSING)
        e = edition.get(key, _MISSING)
        if e is _MISSING:
            if b is _MISSING:
                merged[key] = p          # primary added it
            elif p == b:
                continue                 # edition removed it
            else:
                merged[key] = p
                conflicts.append(path + [key])
            continue
        if b is _MISSING and p != e and not (isinstance(p, dict) and isinstance(e, dict)):
            merged[key] = p
            conflicts.append(path + [key])
            continue
        value, sub = merge3(None if b is _MISSING else b, p, e, path + [key])
        merged[key] = value
        conflicts.extend(sub)
    for key, e in edition.items():
        if key in primary:
            continue
        b = base.get(key, _MISSING)
        if b is _MISSING:
            _insert_after_neighbour(merged, key, e, list(edition))
        elif e != b:
            conflicts.append(path + [key])  # primary removed what edition changed
    return merged, conflicts


def _insert_after_neighbour(merged, key, value, edition_order):
    index = edition_order.index(key)
    anchor = next((k for k in reversed(edition_order[:index]) if k in merged), None)
    items = list(merged.items())
    at = len(items) if anchor is None else [k for k, _ in items].index(anchor) + 1
    items.insert(at, (key, value))
    merged.clear()
    merged.update(items)


def merge_text(base, primary, edition):
    """Merge three JSON texts; return (text, conflicts). base may be None."""
    crlf = "\r\n" in primary
    final_newline = primary.replace("\r\n", "\n").endswith("\n")
    load = lambda t: json.loads(t) if t is not None else {}
    merged, conflicts = merge3(load(base), load(primary), load(edition))
    text = json.dumps(merged, indent=4, ensure_ascii=False)
    if final_newline:
        text += "\n"
    if crlf:
        text = text.replace("\n", "\r\n")
    return text, conflicts


def _read(path):
    if path == "-":
        return None
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("base")
    parser.add_argument("primary")
    parser.add_argument("edition")
    parser.add_argument("-o", "--out", required=True)
    args = parser.parse_args(argv)
    try:
        text, conflicts = merge_text(_read(args.base), _read(args.primary), _read(args.edition))
    except (OSError, ValueError) as error:
        print(f"json_merge3: {error}", file=sys.stderr)
        return 2
    if conflicts:
        for keys in conflicts:
            print("conflict: " + "/".join(map(str, keys)))
        return 1
    with open(args.out, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
