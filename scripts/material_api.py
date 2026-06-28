#!/usr/bin/env python3
"""One-shot CLI for a printer's local material catalog REST API.

Mirrors exactly what FilamentSyncDialog does (src/slic3r/GUI/FilamentSyncDialog.cpp):
all request data is in the query string, the body is always "{}", and the printer
mints its own ids (push is id-less, upsert-by-name). DELETE is by id only.

Stdlib only -- no pip. Examples:

    # list every material on the printer (factory + user)
    python scripts/material_api.py 192.168.0.130 list

    # list only the rows THIS tool registered (have a userMaterial marker)
    python scripts/material_api.py 192.168.0.130 list --mine

    # pull the raw catalog to a json file
    python scripts/material_api.py 192.168.0.130 pull -o catalog.json

    # map the user's custom materials back into CrealityPrint filament presets
    # (one .json per nozzle diameter). NOTE: pulled presets are PARTIAL -- the
    # printer stores only the ~34 fields the slicer pushed, so the rest revert to
    # slicer defaults on import. Import them as NEW presets (File > Import, or a
    # scratch dir); do NOT copy them over your live user filament dir or you will
    # overwrite richer originals. The tool refuses to write there unless --force.
    python scripts/material_api.py 192.168.0.130 presets --out-dir ./pulled_filaments \
        --printer "Creality K2 Plus"

    # push/upsert one material (id-less; printer keys by name)
    python scripts/material_api.py 192.168.0.130 push \
        --param vendor=Custom --param name=MyPLA --param account=<userId>

    # delete by id (only ids of user-registered rows are safe to delete)
    python scripts/material_api.py 192.168.0.130 delete --id <id>
"""
import argparse
import glob
import json
import os
import re
import sys
import urllib.parse
import urllib.request

PORT = 7125

# ---------------------------------------------------------------------------
# pull -> preset mapping
#
# The push side (FilamentSyncDialog::collect_custom_filament_payloads) collapses
# a material's per-nozzle preset variants into ONE upsert whose flat kvParam holds
# the nozzle-independent fields and whose nozzleParam[<nozzle>] holds the per-nozzle
# calibration (pressure_advance / flow). The printer stores kvParam as scalar
# strings. CrealityPrint, however, serializes a USER filament preset with nearly
# every config key wrapped in a single-element array (a per-extruder vector). The
# faithful inverse therefore re-wraps exactly those vector keys.
#
# The authoritative vector-key set is the UNION of a baked-in baseline and
# whatever the local install's user presets reveal (_discover_schema): the
# baseline guarantees the known set even when no install is present, and the
# scan adds any keys a newer CrealityPrint version introduces. The baseline is
# the exact union observed across CrealityPrint 7.0 (v1.7.0.x) user filament
# presets (100 keys).
# ---------------------------------------------------------------------------
FALLBACK_ARRAY_KEYS = {
    "activate_air_filtration", "activate_chamber_layer", "activate_chamber_temp_control",
    "additional_cooling_fan_speed", "bed_type", "chamber_temperature",
    "close_fan_the_first_x_layers",
    "compatible_printers", "compatible_prints", "complete_print_exhaust_fan_speed",
    "cool_cds_fan_start_at_height", "cool_plate_temp", "cool_plate_temp_initial_layer",
    "cool_special_cds_fan_speed", "cooling_perimeter_transition_distance",
    "cooling_slowdown_logic", "customized_plate_temp", "customized_plate_temp_initial_layer",
    "default_filament_colour", "during_print_exhaust_fan_speed", "enable_overhang_bridge_fan",
    "enable_pressure_advance", "enable_special_area_additional_cooling_fan", "eng_plate_temp",
    "eng_plate_temp_initial_layer", "epoxy_resin_plate_temp",
    "epoxy_resin_plate_temp_initial_layer", "fan_cooling_layer_time", "fan_max_speed",
    "fan_min_speed", "filament_adhesiveness_category", "filament_cooling_final_speed",
    "filament_cooling_initial_speed", "filament_cooling_moves", "filament_cost",
    "filament_density", "filament_deretraction_speed", "filament_diameter",
    "filament_end_gcode", "filament_flow_ratio", "filament_is_support", "filament_load_time",
    "filament_loading_speed", "filament_loading_speed_start", "filament_long_retractions_when_cut",
    "filament_max_volumetric_speed", "filament_minimal_purge_on_wipe_tower",
    "filament_multitool_ramming", "filament_multitool_ramming_flow",
    "filament_multitool_ramming_volume", "filament_notes", "filament_ramming_parameters",
    "filament_retract_before_wipe", "filament_retract_lift_above", "filament_retract_lift_below",
    "filament_retract_lift_enforce", "filament_retract_restart_extra",
    "filament_retract_when_changing_layer", "filament_retraction_distances_when_cut",
    "filament_retraction_length", "filament_retraction_minimum_travel",
    "filament_retraction_speed", "filament_settings_id", "filament_shrink",
    "filament_shrinkage_compensation_z", "filament_soluble", "filament_stamping_distance",
    "filament_stamping_loading_speed", "filament_start_gcode", "filament_toolchange_delay",
    "filament_type", "filament_unload_time", "filament_unloading_speed",
    "filament_unloading_speed_start", "filament_vendor", "filament_wipe",
    "filament_wipe_distance", "filament_z_hop", "filament_z_hop_types", "full_fan_speed_layer",
    "hot_plate_temp", "hot_plate_temp_initial_layer", "idle_temperature",
    "material_flow_dependent_temperature", "nozzle_temperature",
    "nozzle_temperature_initial_layer", "nozzle_temperature_range_high",
    "nozzle_temperature_range_low", "overhang_fan_speed", "overhang_fan_threshold",
    "pellet_flow_coefficient", "pressure_advance", "reduce_fan_stop_start_freq",
    "required_nozzle_HRC", "slow_down_for_layer_cooling", "slow_down_layer_time",
    "slow_down_min_speed", "support_material_interface_fan_speed", "temperature_vitrification",
    "textured_plate_temp", "textured_plate_temp_initial_layer",
}
FALLBACK_VERSION = "1.7.0.2"

# SanityPrint extension material fields (FilamentSyncDialog add_shared_params):
# the fork pushes these under their literal slicer config-key names, read via the
# vector-typed ConfigOptionFloats/Ints/Bools accessors -> they are per-extruder
# vector options and must be array-wrapped, but they are absent from older user
# presets so the disk scan won't reveal them. Sourced from the C++, not guessed.
EXTENSION_VECTOR_KEYS = {
    "filament_temp_type", "filament_cooling_smart_zone", "filament_bed_adhesion_strength",
    "filament_thermal_length", "filament_brim_adhesion_coeff", "filament_small_island_threshold",
    "filament_chamber_temp_limit", "filament_is_flexible",
}

# Keys this tool sets as metadata; the printer echoes some back inside kvParam
# (notably the pushed "name"), so the kvParam copy must never overwrite them.
RESERVED_KEYS = {
    "name", "from", "filament_id", "setting_id", "inherits", "instantiation", "version", "type",
}

# Vector keys CrealityPrint writes as an empty list [] (not [""]) when unset. Every
# OTHER string-typed vector key is written as [""] when empty (gcode/colour/notes),
# so an empty scalar must NOT collapse to [] for them.
EMPTY_LIST_KEYS = {"compatible_printers", "compatible_prints"}

# Percent-typed filament options: the push side strips the '%' (format_double_param),
# so the printer stores a bare number; re-append '%' to match CP's canonical form.
# ConfigOptionPercent tolerates the bare number, so this is canonicalization only.
PERCENT_KEYS = {"filament_shrink", "filament_shrinkage_compensation_z"}

_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_QUOTED_RE = re.compile(r'^".*"$', re.DOTALL)
_NUMERIC_RE = re.compile(r"^-?\d+(\.\d+)?$")
_CSTYLE_UNESCAPE = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}


def _unquote_cstyle(s):
    """Reverse the printer's INI/cstyle string serialization.

    The printer stores some string-typed config values double-quoted with cstyle
    escaping -- e.g. filament_ramming_parameters as '"120 100 ..."' (the surrounding
    quotes are PART of the value) and an unset colour/notes as '""'. Emitting that
    verbatim breaks the wipe-tower ramming parser (a leading '"' fails its float
    read) and leaves literal quotes in text fields. Strip one quote layer and
    unescape so the preset carries the raw value CrealityPrint's JSON loader writes.
    Values not in the quoted form pass through unchanged (e.g. raw multiline gcode)."""
    if not isinstance(s, str) or not _QUOTED_RE.match(s):
        return s
    inner, out, i = s[1:-1], [], 0
    while i < len(inner):
        c = inner[i]
        if c == "\\" and i + 1 < len(inner):
            out.append(_CSTYLE_UNESCAPE.get(inner[i + 1], inner[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _is_live_preset_dir(abs_path):
    """True if abs_path resolves inside a CrealityPrint filament-preset directory
    (system or user) -- writing partial pulled presets there would clobber real
    profiles, so it is refused unless --force."""
    p = abs_path.replace("\\", "/").lower()
    return re.search(r"creality[ _]print/.*/(system|user)/.*filament", p) is not None

_SCHEMA_CACHE = None


def _url(ip, params=None):
    base = f"http://{ip}:{PORT}/server/material"
    if params:
        base += "?" + urllib.parse.urlencode(params)
    return base


def _request(method, url):
    # Non-empty body required even for GET-shaped calls: the C++ client always
    # ships "{}" because libcurl needs a body for POST/DELETE. Harmless on GET.
    body = b"{}" if method in ("POST", "DELETE") else None
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=15) as resp:
        return resp.status, resp.read().decode("utf-8", "replace")


def _materials(ip):
    status, body = _request("GET", _url(ip))
    if status != 200:
        sys.exit(f"GET failed: status {status}")
    return json.loads(body).get("result", {}).get("materials", [])


def _discover_schema(ref_dir=None):
    """(array_keys, valid_keys, version, derived_count, existing_names) for the install.

    - array_keys: keys to wrap as single-element arrays. UNION of the baked-in
      baseline, the SanityPrint extension vector keys, and every key seen as a JSON
      list on disk -- so it mirrors whatever this CrealityPrint version writes
      (catching keys a newer version adds) while never under-wrapping the known set.
    - valid_keys: the full universe of config keys seen on disk (plus extensions);
      used to whitelist kvParam so stray push-param aliases / body junk never leak
      into a preset. None when no install was scanned (off-machine) -> no whitelist.
    - version: the MODE across user presets (the value this install stamps).
    - existing_names: every filament preset NAME already present (system + user);
      used to skip duplicates so we never clone an existing (esp. system) preset.

    USER presets are scanned first: they carry the richest vector shape, and a file
    cap must never starve them behind the far more numerous system presets."""
    global _SCHEMA_CACHE
    if _SCHEMA_CACHE is not None:
        return _SCHEMA_CACHE
    roots = []
    if ref_dir:
        roots.append(ref_dir)
    appdata = os.environ.get("APPDATA")
    if appdata:
        roots.append(os.path.join(appdata, "Creality", "Creality Print"))

    derived, all_keys, names, scanned = set(), set(), set(), 0
    user_versions = {}  # version string -> count, across USER presets only
    for root in roots:
        if not root or not os.path.isdir(root):
            continue
        # user presets first, then everything else -- so the cap can't starve them.
        user_glob = os.path.join(root, "**", "user", "**", "filament", "**", "*.json")
        all_glob = os.path.join(root, "**", "filament", "**", "*.json")
        user_paths = list(glob.iglob(user_glob, recursive=True))
        user_set = set(user_paths)
        for path in user_paths + list(glob.iglob(all_glob, recursive=True)):
            if scanned >= 1500:
                break
            try:
                with open(path, encoding="utf-8") as f:
                    d = json.load(f)
            except Exception:
                continue
            if not isinstance(d, dict):
                continue
            scanned += 1
            all_keys |= set(d.keys())
            if isinstance(d.get("name"), str) and d["name"]:
                names.add(d["name"])
            for k, v in d.items():
                if isinstance(v, list):
                    derived.add(k)
            # Version: take the MODE across user presets -- the value this install
            # actually stamps when it writes a user preset, not an arbitrary first
            # hit (system/base presets carry unrelated schema versions).
            ver = d.get("version")
            if path in user_set and isinstance(ver, str) and ver:
                user_versions[ver] = user_versions.get(ver, 0) + 1

    array_keys = derived | FALLBACK_ARRAY_KEYS | EXTENSION_VECTOR_KEYS
    valid_keys = (all_keys | array_keys) if scanned else None
    version = (max(user_versions, key=user_versions.get) if user_versions
               else FALLBACK_VERSION)
    _SCHEMA_CACHE = (array_keys, valid_keys, version, len(derived), names)
    return _SCHEMA_CACHE


def _sanitize_filename(name):
    return re.sub(r'[<>:"/\\|?*\n\r\t]', "_", name).strip() or "material"


def _disambiguate_name(name, taken):
    """Return a unique preset name by inserting a '(pulled)' tag before the
    ' @<printer> <nozzle> nozzle' segment (or at the end if there is none), e.g.
    'PLA X @Creality K2 Plus 0.4 nozzle' -> 'PLA X (pulled) @Creality K2 Plus 0.4
    nozzle'. Numbers the tag if that too is taken."""
    at = name.find(" @")
    head, tail = (name[:at], name[at:]) if at != -1 else (name, "")
    cand = f"{head} (pulled){tail}"
    n = 2
    while cand in taken:
        cand = f"{head} (pulled {n}){tail}"
        n += 1
    return cand


def _material_to_presets(mat, array_keys, valid_keys, version, printer=None):
    """Map one printer material -> a list of (preset_name, pernozzle_names, preset).

    Mirrors the push (FilamentSyncDialog: ONE material per filament_id, with only
    the calibration triplet stored per nozzle in nozzleParam). So this emits ONE
    preset per material -- every nozzle listed in compatible_printers -- and splits
    into separate presets ONLY for nozzles whose calibration actually differs (a
    slicer preset holds a single calibration value, which is the whole reason the
    push tracks it per nozzle). pernozzle_names are the per-nozzle preset names this
    group covers, for duplicate detection against CP's per-nozzle on-disk presets."""
    base = mat.get("base", {})
    kv = mat.get("kvParam", {})
    nozzle_param = mat.get("nozzleParam", {}) or {}
    nozzles = mat.get("nozzleDiameter") or ["0.4"]
    vendor = base.get("brand", "") or "Custom"
    mat_name = base.get("name", "") or "Unnamed"
    ftype = kv.get("filament_type") or base.get("type", "")
    fid = base.get("id", "")

    # Group nozzles by calibration signature: kvParam is shared across nozzles, only
    # nozzleParam[<nz>] varies, so nozzles with identical calibration collapse into
    # one preset and differing ones split -- the minimum set of slicer presets.
    groups, index = [], {}  # groups: [(calib_dict, [nozzles])] in first-seen order
    for nz in nozzles:
        calib = nozzle_param.get(nz) or {}
        sig = json.dumps(calib, sort_keys=True)
        if sig not in index:
            index[sig] = len(groups)
            groups.append((calib, []))
        groups[index[sig]][1].append(nz)

    def per_nozzle_name(nz):
        return f"{mat_name} @{printer} {nz} nozzle" if printer else f"{mat_name} {nz} nozzle"

    out = []
    for calib, gnz in groups:
        cfg = dict(kv)
        cfg.update(calib)  # this group's per-nozzle calibration (pa / flow / enable)
        # Stamp identity so the row is self-describing even if kvParam omitted it.
        cfg["filament_vendor"] = vendor
        if ftype:
            cfg["filament_type"] = ftype
        # Keep the nozzle suffix for a single-nozzle group (matches CP's native
        # per-nozzle naming and the install's dup names); drop it when the preset
        # spans several nozzles, which are instead all listed in compatible_printers.
        if len(gnz) == 1:
            preset_name = per_nozzle_name(gnz[0])
        else:
            preset_name = f"{mat_name} @{printer}" if printer else mat_name
        if printer:
            cfg["compatible_printers"] = [f"{printer} {n} nozzle" for n in gnz]
        # CrealityPrint backfills this from the name on save, but stamp it for
        # exact-shape fidelity with what it writes for user presets.
        cfg["filament_settings_id"] = preset_name

        # Metadata first (matches the shape CrealityPrint writes for user presets:
        # no "type"/"instantiation"/"setting_id"; from=User; empty inherits).
        preset = {"name": preset_name, "from": "User"}
        if fid:
            preset["filament_id"] = fid
        preset["inherits"] = ""
        preset["version"] = version

        for k in sorted(cfg):
            # Never let kvParam's echoed copies overwrite managed metadata (the
            # printer stores the pushed "name" in kvParam), and drop non-identifier
            # junk -- the POST body "{}" leaks in as a literal key.
            if k in RESERVED_KEYS or not _IDENT_RE.match(k):
                continue
            # Whitelist against known config keys (when an install was scanned) so
            # stray push-param aliases can't pollute the preset.
            if valid_keys is not None and k not in valid_keys:
                continue
            v = cfg[k]
            if isinstance(v, list):  # already a vector (compatible_printers)
                preset[k] = v
                continue
            v = _unquote_cstyle(v)  # reverse the printer's INI string encoding
            if k in PERCENT_KEYS and _NUMERIC_RE.match(v):
                v += "%"  # canonical percent form (push stripped the '%')
            if k in array_keys:
                # Vector option: wrap as a single-element array. An empty scalar is
                # [] only for the compatible_* keys; every other string-typed vector
                # key (gcode/colour/notes) is written as [""], not [].
                if v == "":
                    preset[k] = [] if k in EMPTY_LIST_KEYS else [""]
                else:
                    preset[k] = [v]
            else:
                preset[k] = v
        out.append((preset_name, [per_nozzle_name(n) for n in gnz], preset))
    return out


def cmd_list(args):
    mats = _materials(args.ip)
    for m in mats:
        if args.mine and not m.get("userMaterial"):
            continue
        base = m.get("base", {})
        mark = " *" if m.get("userMaterial") else ""
        print(f"{base.get('id',''):<24} {base.get('name',''):<28} "
              f"{base.get('brand',''):<16}{mark}")
    print(f"\n{len(mats)} material(s); * = registered by this tool",
          file=sys.stderr)


def cmd_pull(args):
    mats = _materials(args.ip)
    out = json.dumps(mats, indent=2, ensure_ascii=False)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(out)
        print(f"wrote {len(mats)} material(s) -> {args.out}", file=sys.stderr)
    else:
        print(out)


def cmd_presets(args):
    out_abs = os.path.abspath(args.out_dir)
    # Refuse to clobber real profiles: pulled presets are partial, so writing them
    # over a live CrealityPrint filament dir would overwrite richer originals.
    if _is_live_preset_dir(out_abs) and not args.force:
        sys.exit(f"refusing to write into a CrealityPrint preset dir:\n  {out_abs}\n"
                 "Pulled presets are PARTIAL (only the fields the printer stores; the rest "
                 "revert to slicer defaults) and would overwrite richer originals.\n"
                 "Write to a scratch dir and import as NEW presets, or pass --force to override.")
    if not args.printer:
        print("warning: --printer not set -> presets omit compatible_printers and the "
              "'@<printer> <nozzle> nozzle' name suffix; they load as compatible-with-all "
              "and are harder to tell apart. Pass --printer \"Creality K2 Plus\" to fix.",
              file=sys.stderr)

    mats = _materials(args.ip)
    array_keys, valid_keys, version, derived_count, existing = _discover_schema(args.ref_dir)
    os.makedirs(args.out_dir, exist_ok=True)

    selected = []
    for m in mats:
        if not args.include_factory and not m.get("userMaterial"):
            continue
        name = m.get("base", {}).get("name", "")
        if args.name and args.name.lower() not in name.lower():
            continue
        if not m.get("kvParam"):
            print(f"skip (no kvParam): {name}", file=sys.stderr)
            continue
        selected.append(m)

    written = overwritten = created_new = skipped_dup = 0
    taken = set(existing)  # preset names already in the install + assigned this run
    used = set()           # sanitized filenames already written this run
    for m in selected:
        for preset_name, pernozzle_names, preset in _material_to_presets(
                m, array_keys, valid_keys, version, args.printer):
            # A duplicate of an existing system/user preset: the user already has it
            # if its own (collapsed) name exists, or every per-nozzle name it covers
            # exists. Skip by default so we never clone (and an import never
            # conflicts); --create-new instead writes it under a disambiguated
            # '(pulled)' name beside the original.
            is_dup = preset_name in existing or all(n in existing for n in pernozzle_names)
            if is_dup:
                if not args.create_new:
                    skipped_dup += 1
                    continue
                preset_name = _disambiguate_name(preset_name, taken)
                preset["name"] = preset_name
                preset["filament_settings_id"] = [preset_name]
                created_new += 1
            taken.add(preset_name)

            fname = _sanitize_filename(preset_name) + ".json"
            if fname in used:  # distinct material names can sanitize to one file
                stem, ext = os.path.splitext(fname)
                n = 2
                while f"{stem} ({n}){ext}" in used:
                    n += 1
                fname = f"{stem} ({n}){ext}"
                print(f"warning: filename collision -> writing {fname}", file=sys.stderr)
            used.add(fname)
            path = os.path.join(args.out_dir, fname)
            if os.path.exists(path):
                overwritten += 1
            with open(path, "w", encoding="utf-8") as f:
                json.dump(preset, f, indent=4, ensure_ascii=False)
            written += 1

    dup_note = ""
    if skipped_dup:
        dup_note = (f"; skipped {skipped_dup} already in your install "
                    "(use --create-new to write them as new '(pulled)' presets)")
    elif created_new:
        dup_note = f"; {created_new} written as new '(pulled)' presets (duplicates of existing)"
    print(f"wrote {written} preset(s) ({overwritten} overwritten) from "
          f"{len(selected)} material(s){dup_note}\n  -> {out_abs}", file=sys.stderr)
    print("note: pulled presets are PARTIAL -- only the fields the printer stores; other "
          "fields revert to slicer defaults on import. filament_id is the printer's id, not "
          "the original. Import as NEW presets; don't overwrite your user filament dir.",
          file=sys.stderr)
    src = (f"{derived_count} derived from local install + baseline"
           if derived_count else "baked-in baseline (no local install found)")
    print(f"schema: {len(array_keys)} vector keys ({src}), version {version}",
          file=sys.stderr)


def cmd_push(args):
    params = {}
    for kv in args.param:
        if "=" not in kv:
            sys.exit(f"bad --param (need key=value): {kv}")
        k, v = kv.split("=", 1)
        params[k] = v
    if "vendor" not in params:
        sys.exit("push requires at least --param vendor=<...> (the only required field)")
    status, body = _request("POST", _url(args.ip, params))
    print(f"POST status {status}: {body[:200]}")
    sys.exit(0 if status == 200 else 1)


def cmd_delete(args):
    status, body = _request("DELETE", _url(args.ip, {"id": args.id}))
    print(f"DELETE status {status}: {body[:200]}")
    sys.exit(0 if status == 200 else 1)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("ip", help="printer IP, e.g. 192.168.0.130")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("list", help="list materials")
    s.add_argument("--mine", action="store_true", help="only tool-registered rows")
    s.set_defaults(func=cmd_list)

    s = sub.add_parser("pull", help="dump full raw catalog as json")
    s.add_argument("-o", "--out", help="write to file instead of stdout")
    s.set_defaults(func=cmd_pull)

    s = sub.add_parser("presets", help="map materials -> CrealityPrint filament preset json")
    s.add_argument("--out-dir", default="./pulled_filaments",
                   help="directory to write preset .json files (default: ./pulled_filaments)")
    s.add_argument("--printer",
                   help='compatible printer name to stamp, e.g. "Creality K2 Plus"; '
                        "appended with the nozzle to form compatible_printers and the preset name")
    s.add_argument("--name", help="only materials whose name contains this substring")
    s.add_argument("--include-factory", action="store_true",
                   help="also map factory (non-user) materials (note: names often collide "
                        "with shipped system presets, which shadow them on load)")
    s.add_argument("--create-new", action="store_true",
                   help="instead of skipping materials that already exist as a system/user "
                        "preset, write them under a disambiguated '(pulled)' name")
    s.add_argument("--force", action="store_true",
                   help="allow writing into a live CrealityPrint preset dir (clobbers originals)")
    s.add_argument("--ref-dir",
                   help="CrealityPrint config root to derive the preset schema from "
                        "(default: %%APPDATA%%/Creality/Creality Print)")
    s.set_defaults(func=cmd_presets)

    s = sub.add_parser("push", help="upsert one material (id-less, by name)")
    s.add_argument("--param", action="append", default=[],
                   help="key=value query param; repeatable (vendor required)")
    s.set_defaults(func=cmd_push)

    s = sub.add_parser("delete", help="delete one material by id")
    s.add_argument("--id", required=True)
    s.set_defaults(func=cmd_delete)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
