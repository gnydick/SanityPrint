# Implementation Brief: Parameterize Filament Behavior (remove hardcoded filament types)

> **Purpose of this doc.** A self-contained prompt/spec. If you are starting fresh with no prior
> context, read this top-to-bottom and you will know exactly what to build, why, where, the exact
> values to preserve, how to build/test on this machine, and in what order. Grep patterns and
> function names are given instead of relying on line numbers (which drift).

---

## 0. TL;DR

CrealityPrint (an OrcaSlicer/Bambu-Studio fork) currently decides a bunch of slicing behavior with
hardcoded `if (filament_type == "PLA")`-style switches scattered across ~11 sites. That makes
filament *types* a closed, compiled-in set: users cannot create a genuinely new material, only reuse
an existing type's behavior.

**Goal: give the end user complete control by making behavior *data on the filament preset* instead
of hardcoded type logic.** Every type-keyed behavior becomes a per-filament config field. The engine
reads the field. The `filament_type` string degrades to a pure label (display + AMS matching). After
this, a new material is just a new preset with its own values — no base types, no inheritance, no
central type table, no recompile.

This **supersedes and removes** an earlier "base-type inheritance" implementation (a
`FilamentTypeRegistry`) that lives on branch `feature/arbitrary-filament-types`. See §7.

---

## 1. Locked design decisions (do not relitigate)

These were decided deliberately:

1. **Byte-for-byte non-regression for built-in types.** After the change, the built-in types
   (PLA/ABS/PETG/…) must slice **identically** to today. Every current hardcoded constant is
   reproduced exactly as data. This is the hard constraint; §5 is the source of truth for it.
2. **Preset-only. No central type table at slice time.** The shipped/running engine must NOT consult
   any `type → value` map while slicing. Behavior comes only from per-filament config fields.
3. **Everything lives in the filament file. No hardcoded types.** A filament preset is the complete,
   self-contained definition of the material.
4. **A `type → value` mapping may exist ONLY in one-time migration tooling**, never in the runtime
   engine (see §8/§9). That is how byte-for-byte is achieved without a runtime table.
5. **Packaged migration tool** upgrades users' existing custom filament presets in `data_dir`.

---

## 2. Architecture

- Add ~8 new per-filament (per-extruder, vector-typed) config fields to the filament config.
- Rewrite each of the ~11 hardcoded sites to read its field via `.get_at(extruder_id)` instead of
  switching on `filament_type`.
- Set each field's **config default** to the current "else"/fallback value (§4), so an unset field
  behaves like today's default material.
- Ship the built-in profiles pre-populated with exact per-type values (§5) via a one-time migration
  script over `resources/profiles/**/filament/*.json` (§8).
- Ship a migration tool that does the same for user presets in `data_dir` (§9).
- Expose the new fields in the filament-settings UI (§10) so any filament is fully editable.
- Remove the entire `FilamentTypeRegistry`/inheritance layer (§7).

---

## 3. New filament config fields

All are per-extruder vectors (same shape as existing `filament_*` options). Names are proposals —
bikeshed freely, but keep them consistent everywhere.

| Field | Config type | Default | Replaces (site) |
|---|---|---|---|
| `filament_temp_type` | `coInts` (enum 0=HighTemp,1=LowTemp,2=HighLowCompatible,3=Undefine) | `3` (Undefine) | multi-material temp compatibility |
| `filament_cooling_smart_zone` | `coBools` | `0` | smart cooling slowdown zones |
| `filament_bed_adhesion_strength` | `coFloats` (MPa; engine multiplies by 1e6) | `0.02` | support-spot bed adhesion yield |
| `filament_thermal_length` | `coFloats` | `200` | brim/warp thermal length |
| `filament_brim_adhesion_coeff` | `coFloats` | `1` | brim-width adhesion coefficient |
| `filament_small_island_threshold` | `coFloats` | `10` | small-island slice threshold |
| `filament_chamber_temp_limit` | `coInts` (0 = no limit/no warning) | `0` | chamber-temp safety warning |
| `filament_is_flexible` | `coBools` | `0` | PLA-supports-TPU recommendation |

**Already a field — reuse, do not add:** nozzle abrasiveness uses the existing `required_nozzle_HRC`.
Just read it directly and delete the type-based fallback (see §6, PresetBundle).

### Where to declare fields
- **`src/libslic3r/PrintConfig.hpp`** — add to the filament config struct (the `PRINT_CONFIG_CLASS`
  block containing `filament_soluble`, `chamber_temperature`, etc.). Grep: `filament_soluble))`.
- **`src/libslic3r/PrintConfig.cpp`** — add a `def = this->add("filament_xxx", coFloats); ...` block
  for each. Copy the pattern of an existing filament option. Grep anchor:
  `this->add("filament_adhesiveness_category"` (a good template; it is a per-filament `coInts`).
  Set `def->mode = comAdvanced;` and a sane `def->set_default_value(...)`.
- **`src/libslic3r/Preset.cpp`** — add every new key to the FILAMENT preset key lists so they save
  and load. Grep: `required_nozzle_HRC` and `"chamber_temperature"` — there are static vectors of
  filament option names; the new keys must be in the same place(s).

---

## 4. (See §3 defaults) — defaults equal the current "else" branch of each switch.

---

## 5. EXACT per-type values (byte-for-byte source of truth)

This is the migration table. For each built-in `filament_type`, the migration (§8/§9) writes these
field values. Anything not listed gets the field default (§3). Values are the literal current
constants; verify each against the live code before shipping.

### `filament_temp_type` (from `resources/info/filament_info.json`)
- **HighTemp (0):** ABS, ASA, PC, PA, PA-CF, PA-GF, PA6-CF, PET-CF, PPS, PPS-CF, PPA-CF, PPA-GF, ABS-GF, ASA-Aero
- **LowTemp (1):** PLA, TPU, PLA-CF, PLA-AERO, PVA, BVOH
- **HighLowCompatible (2):** HIPS, PETG, PE, PP, EVA, PE-CF, PP-CF, PP-GF, PHA
- **Undefine (3):** everything else (e.g. PCTG)

### `filament_cooling_smart_zone` (CoolingBuffer)
- `true`: PLA, PETG, ABS. Else `false`.

### `filament_bed_adhesion_strength` MPa (SupportSpotsGenerator)
- PLA → `0.02`; PET → `0.3`; PETG → `0.3`; ABS → `0.1`; ASA → `0.1`. Else `0.02`.
  (Engine currently returns `value * 1e6`; keep that multiply at the read site.)

### `filament_thermal_length` (Model::getThermalLength)
- ABS → `100`; PA-CF → `100`; PET-CF → `100`; PC → `40`; TPU → `1000`. Else `200`.

### `filament_brim_adhesion_coeff` (Brim.cpp & ModelInstance.cpp — identical logic)
- PETG → `2`; PCTG → `2`; TPU → `0.5`. Else `1`.

### `filament_small_island_threshold` (GCode.cpp)
- PETG → `20`. Else `10`.

### `filament_chamber_temp_limit` °C (ConfigManipulation::check_chamber_temperature `recommend_temp_map`)
- PLA → `45`; PLA-CF → `45`; PVA → `45`; TPU → `50`; PETG → `55`; PCTG → `55`; PETG-CF → `55`.
  Else `0` (no warning).

### `filament_is_flexible` (Tab.cpp support_TPU)
- TPU → `true`. Else `false`.
- **Note / micro-decision:** the current recommendation fires when the *support* filament is **PLA**
  and the model contains TPU/TPU-AMS. Reproducing that exactly needs PLA flagged as "good flexible
  support". Since this is a *recommendation dialog, not G-code*, the agreed simplification is to
  generalize it to "a non-flexible filament supporting a flexible one" using only `filament_is_flexible`
  (one flag). If strict byte-for-byte on this UI hint is required, add a second bool
  `filament_good_flexible_support` (PLA=true) and gate on it instead.

---

## 6. Sites to rewrite (engine + UI)

Each replaces a `filament_type`/`materialName == "X"` switch with a field read. On
`feature/arbitrary-filament-types` these currently route through
`FilamentTypeRegistry::effective_type(...)`; on this work both the registry call AND the original
type switch get replaced by a plain field read.

| File | Function / anchor | Replace with |
|---|---|---|
| `src/libslic3r/FDM/Filament.cpp` | `get_filament_temp_type`, `check_multi_filaments_compatibility` | read `filament_temp_type` field; drop registry delegation |
| `src/libslic3r/GCode/CoolingBuffer.cpp` | grep `cooling_slowdown_smart_zone` | `if (config.filament_cooling_smart_zone.get_at(extruder_id))` |
| `src/libslic3r/SupportSpotsGenerator.hpp` | `get_bed_adhesion_yield_strength()` | `return params.filament_bed_adhesion_strength * 1e6;` (thread the value into `Params`) |
| `src/libslic3r/GCode.cpp` | grep `slice_threshold` (small islands) | read `filament_small_island_threshold` |
| `src/libslic3r/Model.cpp` | `Model::getThermalLength` | read `filament_thermal_length` (needs it in `ExtruderParams`; see note) |
| `src/libslic3r/Brim.cpp` | `getBrimWidth`/adhesionCoeff loop | read `filament_brim_adhesion_coeff` (via `ExtruderParams`) |
| `src/libslic3r/ModelInstance.cpp` | `getadhesionCoeff` | read `filament_brim_adhesion_coeff` (via `ExtruderParams`) |
| `src/slic3r/GUI/Tab.cpp` | grep `support_TPU` (2 spots) | use `filament_is_flexible` per §5 note |
| `src/slic3r/GUI/ConfigManipulation.cpp` | `check_chamber_temperature` | read `filament_chamber_temp_limit`; drop `recommend_temp_map` |
| `src/slic3r/GUI/GUI_App.cpp` | `is_support_filament` | rethink without type; flexible/soluble flags or simplify |
| `src/slic3r/GUI/GUI_App.cpp` | grep `fdm_filament_pla` (vendor import `inherits`) | drop type→profile map; import from `fdm_filament_common` (vendor JSON supplies values) |
| `src/libslic3r/PresetBundle.cpp` | `get_required_hrc_by_filament_type` | read `required_nozzle_HRC` field directly; drop the type map |
| `src/slic3r/GUI/AmsMappingPopup.cpp` | `is_match_material` | exact `filament_type` name compare (revert any base-aware logic) |

**`Model::extruderParamsMap` note:** `Model::getThermalLength`, `Brim`, and `ModelInstance` read
material info from `Model::extruderParamsMap` (struct `ExtruderParams` in
`src/libslic3r/ModelCommon.hpp`, populated by `Model::setExtruderParams` in `Model.cpp` from
`config.opt_string("filament_type", i)`). Extend `ExtruderParams` to carry the new numeric fields
(thermal length, brim coeff) and populate them in `setExtruderParams` from the new config options,
so the engine sites read them off `extruderParamsMap` exactly like `materialName` today.

---

## 7. Remove the inheritance / registry machinery

This work supersedes the `FilamentTypeRegistry` approach. Delete / revert:

- `src/libslic3r/FilamentTypeRegistry.hpp` and `.cpp` — delete.
- `src/libslic3r/creality.cmake` — remove the two `FilamentTypeRegistry.*` entries.
- `resources/info/filament_info.json` — remove the `base_types` and `base_type` maps. (The temp
  arrays are no longer read at runtime either; keep a copy in the migration tool to populate
  `filament_temp_type`.)
- `tests/libslic3r/test_filament_type_registry.cpp` and its `filament_registry_test` target in
  `tests/libslic3r/CMakeLists.txt` — delete (replace with field-based tests, §13).
- `src/slic3r/GUI/CreatePresetsDialog.{hpp,cpp}` — remove the base-type combobox / `curr_filament_base`
  / `add_custom_type` calls (grep `m_filament_base_combobox`, `base_types()`). The Type field stays an
  editable combobox (that part is wanted); just drop the base picker and base-matching, and instead
  let a new filament inherit values by cloning a chosen source preset.
- Drop the `custom_filament_types.json` persistence concept and the `.gitignore` entry for it.
- The reverted/removed sites in §6 also undo the Phase 2/3 `effective_type` routing.

> Net effect vs upstream: the original `if (type=="X")` switches and the registry both disappear,
> replaced by field reads. Confirm with: `grep -rn 'filament_type ==\|materialName ==\|effective_type\|FilamentTypeRegistry' src/` should return ~nothing behavior-bearing afterward.

---

## 8. Built-in profile migration (one-time, in-repo)

Write a script (PowerShell or Python; it edits JSON, no app build needed) that:

1. Walks every `resources/profiles/**/filament/*.json` (per-vendor; there is **no** shared global
   base — each vendor has its own `fdm_filament_<type>` + `fdm_filament_common`).
2. Resolves each preset's effective `filament_type` (follow `inherits` within the same vendor folder
   if the leaf doesn't set it; the `fdm_filament_<type>` bases set it).
3. Writes the new field values per the §5 tables onto the appropriate profile. Prefer writing onto
   the per-vendor `fdm_filament_<type>` base profiles so concrete presets inherit them; write onto a
   concrete preset directly only when no matching type-base exists.
4. Leaves values that already equal the default unset (to keep diffs small) — optional.

Run once, eyeball the diff, commit the modified profiles. This is the only place the §5 table lives
for built-ins; it is build/tooling data, not shipped engine code.

---

## 9. Packaged migration tool (user presets)

Ship a tool (could be the same script, invoked on first run / from a menu) that applies the §5
mapping to the user's existing filament presets under `data_dir()` (resolve `data_dir` the same way
the app does — see `set_data_dir`/`data_dir` in `src/libslic3r/Utils.cpp`). It reads each user
preset's `filament_type`, fills any missing new fields from the §5 table, and saves. After running,
nothing keys on type. The §5 table lives only in this tool, satisfying the "no runtime type table"
rule. Decide packaging: a one-shot upgrade on version bump is the least-friction option.

---

## 10. UI: expose the fields

In the filament settings tab (`src/slic3r/GUI/Tab.cpp`, `TabFilament::build()` / its page/optgroup
construction — grep for an existing filament optgroup like `"Cooling"` or `chamber_temperature`), add
the new fields under a collapsed/advanced **"Material properties"** group so casual users aren't
overwhelmed but power users have full control. Each field needs a label/tooltip in `PrintConfig.cpp`.

The filament **Type** field itself: leave the editor's Type as a select-only dropdown (already
reverted on the base branch in commit `36dedfff`); the editable Type lives in **Create New Filament**
(`CreatePresetsDialog`). No base picker (removed in §7).

---

## 11. Persistence / plumbing checklist

- New keys added to the filament key lists in `src/libslic3r/Preset.cpp` (so save/load/round-trip).
- Defaults set in `PrintConfig.cpp` (§3).
- If any field affects slicing invalidation, add it to the relevant `Print::apply`/option-class lists
  in `src/libslic3r/Print.cpp` (grep `filament_soluble` there for the pattern) so changing it
  re-slices correctly.
- `ExtruderParams` extended + populated (`Model.cpp setExtruderParams`) for the engine-side reads.

---

## 12. Build & test on THIS machine

There is a committed project skill **`build-crealityprint`** with the full toolchain setup — read it.
Key facts and prebuilt scripts:

- Windows, VS 2022 Build Tools. **Pin CMake 3.31.12** (CMake 4.x breaks this legacy tree).
- Deps prefix: `D:\cpbuild\OrcaSlicer_dep\usr\local`. Build tree on `D:` (`D:\cpbuild\app`).
- Release build uses `-DPROJECT_VERSION_EXTRA=Release` and `BUILD_ID=4472` → version 7.1.1.4472.
- **App build:** `D:\cpbuild\build_app.ps1` (configures `-DSLIC3R_BUILD_TESTS=OFF`, builds
  `ALL_BUILD`). Logs to `D:\cpbuild\app_build.log`; sentinel `D:\cpbuild\app.done` =
  `build-exit-<N>`; binaries land in `D:\cpbuild\app\src\Release\` (`CrealityPrint.exe`,
  `CrealityPrint_Slicer.dll`). Run it `run_in_background`.
- **Unit-test build/run:** `D:\cpbuild\build_tests.ps1` (reconfigures `-DSLIC3R_BUILD_TESTS=ON`,
  builds a single small test target, runs it with app DLLs on `PATH`). Sentinel
  `D:\cpbuild\tests.done` = `tests-exit-<N>`. Catch2 v2 (`#include <catch2/catch.hpp>`).
- **Gotchas:**
  - `build_app.ps1` and `build_tests.ps1` reconfigure the same cache (TESTS OFF/ON) — never run them
    concurrently; run sequentially.
  - **Close any running `CrealityPrint.exe` before building** — a running app holds
    `CrealityPrint_Slicer.dll` open and the link fails with `LNK1104: cannot open file ...dll`. Check
    `Get-Process -Name CrealityPrint`.
  - The build log contains NUL bytes (MSBuild); strip with `-replace "\0",""` when tailing.
  - No `/WX` (warnings are not errors), but `-Werror=return-type` is on — make all paths return.
- libslic3r sources: explicit list in `src/libslic3r/CMakeLists.txt` plus appends in
  `src/libslic3r/creality.cmake` (new `.cpp` files must be registered in one of these).

---

## 13. Verification (prove byte-for-byte)

1. **Unit test:** for each built-in `filament_type`, assert the migrated profile's field values equal
   the §5 constants (parse the migrated JSON, or test the read sites with a synthetic config).
2. **Golden G-code diff (strongest):** before starting, slice a small reference model with a handful
   of representative built-in filaments (PLA, PETG, ABS, TPU, PA-CF, PC) and save the G-code. After
   the change, slice the same and `diff`. Byte-for-byte means identical G-code (modulo timestamps).
   This is the real acceptance test for decision #1.
3. Build the app clean (`build-exit-0`) and smoke-test: create a custom filament with arbitrary
   values, confirm those values drive slicing (e.g. set an extreme `filament_thermal_length` and see
   brim change).

---

## 14. Branch strategy

- Base off **`feature/arbitrary-filament-types`** (it has the inheritance impl to remove + the
  `36dedfff` Field.cpp revert). Name e.g. `feature/parameterize-filament-types`.
- Origin is the user's fork `gnydick/CrealityPrint`. Commit in logical chunks; push when asked.
- End git commit messages with:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

## 15. Open micro-decisions to confirm with the user

1. **`filament_temp_type`**: explicit enum field (recommended, guarantees byte-for-byte) vs. deriving
   multi-material compatibility from existing `nozzle_temperature_range_low/high` overlap (no field,
   more physical, NOT guaranteed byte-for-byte). Default to the explicit field.
2. **`filament_is_flexible`** one flag (generalized UI hint) vs. add `filament_good_flexible_support`
   for strict reproduction (see §5 note). Default to one flag.
3. Field names / which UI group / advanced-vs-simple visibility.
4. Migration-tool packaging (auto on version bump vs. manual menu action).

---

## 16. Suggested execution order

1. Add the config fields + defaults + tooltips (`PrintConfig.hpp/.cpp`) and Preset key lists
   (`Preset.cpp`). Build — nothing reads them yet, should be green.
2. Extend `ExtruderParams` + `setExtruderParams`.
3. Rewrite the engine sites (§6, libslic3r) to read fields. Build.
4. Rewrite the GUI sites (§6, slic3r/GUI). Build.
5. Remove the registry/inheritance machinery (§7). Build.
6. Write + run the built-in migration script (§8); commit modified profiles.
7. Add the UI group (§10). Build.
8. Build the migration tool for user presets (§9).
9. Verification (§13): unit tests + golden G-code diff. Fix until byte-for-byte.
10. Commit logically, push.
