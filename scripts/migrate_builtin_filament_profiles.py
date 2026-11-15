#!/usr/bin/env python3
"""
One-time migration: populate the 8 new parameterized filament behavior fields
onto the vendor fdm_filament_<type> base profiles (and any concrete profiles that
set filament_type directly without inheriting from a type base).

Values are loaded from resources/profiles_template/material_behavior.json
(originally §5 of parameterize-filament-types-prompt.md).
Run from the repo root:
    python scripts/migrate_builtin_filament_profiles.py

Pass --dry-run to preview without writing.
"""

import argparse
import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).parent.parent
PROFILES_DIR = REPO_ROOT / "resources" / "profiles"

# ---------------------------------------------------------------------------
# Tables — loaded from the shared data file (single source of truth, also read
# by CreatePresetsDialog.cpp and the GUI_App user-preset migration).
# Per-table coercion preserves the script's historical string output:
# float tables render "100.0"/"2.0", int tables render "0"/"45".
# ---------------------------------------------------------------------------
_TABLES_PATH = REPO_ROOT / "resources" / "profiles_template" / "material_behavior.json"
_TABLES = json.loads(_TABLES_PATH.read_text(encoding="utf-8"))

# filament_temp_type: 0=HighTemp, 1=LowTemp, 2=HighLowCompatible, 3=Undefine
TEMP_TYPE = {k: int(v) for k, v in _TABLES["temp_type"].items()}  # default 3 (Undefine)

COOLING_SMART_ZONE = set(_TABLES["cooling_smart_zone_types"])  # true; else false

BED_ADHESION = {k: float(v) for k, v in _TABLES["bed_adhesion"].items()}  # default 0.02

THERMAL_LENGTH = {k: float(v) for k, v in _TABLES["thermal_length"].items()}  # default 200.0

BRIM_ADHESION_COEFF = {k: float(v) for k, v in _TABLES["brim_adhesion_coeff"].items()}  # default 1.0

SMALL_ISLAND_THRESHOLD = {k: float(v) for k, v in _TABLES["small_island_threshold"].items()}  # default 10.0

CHAMBER_TEMP_LIMIT = {k: int(v) for k, v in _TABLES["chamber_temp_limit"].items()}  # default 0

IS_FLEXIBLE = set(_TABLES["flexible_types"])  # true; else false

# Defaults (used to decide whether to skip writing a key)
DEFAULTS = {
    "filament_temp_type": int(_TABLES["temp_type_default"]),
    "filament_cooling_smart_zone": 0,
    "filament_bed_adhesion_strength": float(_TABLES["bed_adhesion_default"]),
    "filament_thermal_length": float(_TABLES["thermal_length_default"]),
    "filament_brim_adhesion_coeff": float(_TABLES["brim_adhesion_coeff_default"]),
    "filament_small_island_threshold": float(_TABLES["small_island_threshold_default"]),
    "filament_chamber_temp_limit": int(_TABLES["chamber_temp_limit_default"]),
    "filament_is_flexible": 0,
}


def values_for_type(ft: str) -> dict:
    """Return {field: value} for the given filament type string, omitting defaults."""
    result = {}
    v = TEMP_TYPE.get(ft, 3)
    if v != DEFAULTS["filament_temp_type"]:
        result["filament_temp_type"] = [str(v)]

    v = 1 if ft in COOLING_SMART_ZONE else 0
    if v != DEFAULTS["filament_cooling_smart_zone"]:
        result["filament_cooling_smart_zone"] = [str(v)]

    v = BED_ADHESION.get(ft, 0.02)
    if v != DEFAULTS["filament_bed_adhesion_strength"]:
        result["filament_bed_adhesion_strength"] = [str(v)]

    v = THERMAL_LENGTH.get(ft, 200.0)
    if v != DEFAULTS["filament_thermal_length"]:
        result["filament_thermal_length"] = [str(v)]

    v = BRIM_ADHESION_COEFF.get(ft, 1.0)
    if v != DEFAULTS["filament_brim_adhesion_coeff"]:
        result["filament_brim_adhesion_coeff"] = [str(v)]

    v = SMALL_ISLAND_THRESHOLD.get(ft, 10.0)
    if v != DEFAULTS["filament_small_island_threshold"]:
        result["filament_small_island_threshold"] = [str(v)]

    v = CHAMBER_TEMP_LIMIT.get(ft, 0)
    if v != DEFAULTS["filament_chamber_temp_limit"]:
        result["filament_chamber_temp_limit"] = [str(v)]

    v = 1 if ft in IS_FLEXIBLE else 0
    if v != DEFAULTS["filament_is_flexible"]:
        result["filament_is_flexible"] = [str(v)]

    return result


NEW_FIELDS = set(DEFAULTS.keys())


def migrate_file(path: pathlib.Path, dry_run: bool) -> bool:
    """Migrate a single JSON file. Returns True if changed."""
    try:
        text = path.read_text(encoding="utf-8")
        data = json.loads(text)
    except Exception as e:
        print(f"  SKIP {path.name}: {e}", file=sys.stderr)
        return False

    # Only act on profiles that directly declare filament_type
    ft_raw = data.get("filament_type")
    if not ft_raw:
        return False
    ft = ft_raw[0] if isinstance(ft_raw, list) else str(ft_raw)

    new_fields = values_for_type(ft)
    changed = False
    for k, v in new_fields.items():
        if k not in data:
            data[k] = v
            changed = True

    if not changed:
        return False

    print(f"  {'DRY ' if dry_run else ''}WRITE {path.relative_to(REPO_ROOT)}")
    for k, v in new_fields.items():
        if k not in (json.loads(text) or {}):
            print(f"    + {k} = {v[0]}")

    if not dry_run:
        path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="Print changes without writing")
    args = parser.parse_args()

    total = modified = 0
    for vendor_dir in sorted(PROFILES_DIR.iterdir()):
        filament_dir = vendor_dir / "filament"
        if not filament_dir.is_dir():
            continue
        for jf in sorted(filament_dir.glob("*.json")):
            total += 1
            if migrate_file(jf, args.dry_run):
                modified += 1

    print(f"\n{'[DRY RUN] ' if args.dry_run else ''}Done: {modified}/{total} files updated.")


if __name__ == "__main__":
    main()
