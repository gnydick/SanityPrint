# Base Filaments — Design

**Date:** 2026-06-11
**Branch:** `feature/better-base-filaments`
**Status:** Approved pending user review

## Problem

Creating a custom filament requires a concrete system preset of that material,
compatible with the exact target printer, to inherit from. Material coverage is
spotty (no PC preset exists for K2 0.4 or K1 SE 0.4), so for those printers a
PC filament can only be cloned from a wrong-material seed with wrong-by-default
values. The abstract `fdm_filament_*` bases cannot fill the gap: profiles with
`instantiation: false` are never loaded as runtime presets (`PresetBundle.cpp:4121`),
carry no `filament_id` (skipped at `CreatePresetsDialog.cpp:1553`), and bind no
`compatible_printers` (skipped at `:1492`). New materials (PCTG, PEKK) dead-end
entirely.

The codebase already contains a dormant solution: a "Template" vendor
(`resources/profiles_template/Template/`) of printer-agnostic material profiles,
with explicit loader exemptions for the instantiation rule (`PresetBundle.cpp:4121`)
and the filament-id requirement (`:4212`). Today it is only consulted by the
create-*printer* dialog. The create-*filament* dialog never sees it.

## Design principle

Material knowledge lives in data files that can be updated without rebuilding —
never in C++ tables. Templates are the user-maintained base layer; editing or
adding a material is a file edit.

## Part A — Template-based filament creation

### A1. Load the Template vendor globally (hidden)

- At system-preset load time, additionally load the Template vendor **directly
  from `resources/profiles_template/`** (same path and API the printer dialog
  already uses, `CreatePresetsDialog.cpp:2583-2586`) into the global
  `PresetBundle`. Deliberately bypass the `%APPDATA%\Sanity\system\` copy
  pipeline so template edits go live on next launch with no vendor version bump.
- Force `is_visible = false` on all loaded template presets: they must never
  appear in the filament picker and cannot be sliced with directly. Inheritance
  resolution finds invisible presets, so parent links still work.
- Verify the config wizard does not list "Template" as an installable vendor
  (it defines no machines; expected non-issue).

### A2. Register the orphaned PC template

`filament_pc_template.json` exists on disk but is missing from `Template.json`'s
`filament_list` — add the entry. (Shipped data bug; PC is the only orphan.)

### A3. Create Filament dialog offers templates

In `CreateFilamentPresetDialog`:

- `get_all_filament_presets()` includes the globally loaded template presets in
  the candidate map, and feeds their `filament_type` values into
  `m_system_filament_types_set` (the current harvest at `:1562-1565` skips
  id-less presets, so templates need explicit inclusion). Authoring a template
  for a new material thereby makes the type selectable with zero code changes.
- `get_filament_presets_by_machine()` builds each printer row's seed combo in
  two sections separated by unselectable delimiter rows, reusing the existing
  `set_label_marker` / `LABEL_ITEM_DISABLED` mechanism from
  `PresetComboBoxes.cpp:315-362,773`:

  ```
  ── Templates ─────────────────   (unselectable)
  Generic ABS template             all Template-vendor presets,
  …                                sorted by filament type, A→Z (ascending)
  Generic TPU template
  ── Existing presets ──────────   (unselectable)
  current candidates, current order (type-matching first)
  ```

  All templates appear for every printer (templates are printer-agnostic).
  When a template matching the selected filament type exists, it is the
  pre-selected default for printers with no type-matching concrete preset;
  a type-matching concrete preset (e.g. `Hyper PC` on K2 Plus) still wins
  where one exists.
- Create path: template seeds flow through the existing
  `clone_presets_for_filament` call. The minted user preset **keeps
  `inherits: "<template name>"`** — templates are globally resident system
  presets, so the parent resolves at every launch, the editor shows
  modified-vs-parent state, and template tuning propagates to derived
  filaments. (No flattening.)

### A4. Externalize the material-behavior tables

The type→behavior maps (temp type, bed adhesion, thermal length, brim adhesion,
chamber limit, + special cases) currently exist in three hand-synced copies:
`seed_material_behavior` (`CreatePresetsDialog.cpp:1224`),
`migrate_user_filament_presets` (`GUI_App.cpp:2921`), and
`scripts/migrate_builtin_filament_profiles.py`. Replace with one data file —
`resources/profiles_template/material_behavior.json` — read by both C++ sites;
the Python script reads the same file. Template-seeded creation barely needs
these tables (behavior fields inherit from the template); they backstop
cross-type clones from machine leaves.

## Part B — Tuned Generic PC system presets

New concrete leaves in `resources/profiles/Creality/filament/`, registered in
`Creality.json` (additive entries; version bump required to propagate to
existing installs):

- `Generic PC @Creality K2 0.4 nozzle.json` — values cloned from
  `Generic PC @Creality K2 Pro 0.4 nozzle` (nearest machine class), adjusted
  for plain-K2 hardware.
- `Generic PC @Creality K1 SE 0.4 nozzle.json` — values cloned from
  `Generic PC @Creality K1C 0.4 nozzle`, with chamber-temperature control
  disabled (K1 SE has no chamber heater) and adhesion/cooling adjusted.
- K1C: `Generic PC @Creality K1C 0.4 nozzle.json` already ships. First
  implementation step: identify the user's installed K1C profile name. If it is
  stock `Creality K1C 0.4 nozzle`, diagnose why the existing preset is not
  offered; if it is a variant (e.g. K1C 2025 / CFS), add the matching file.

Each new leaf: `inherits: fdm_filament_pc`, `compatible_printers` bound to its
printer, unique `filament_id`/`setting_id` following the existing Generic PC
convention (read sibling files for the scheme). These are starting points and
need a validation print before being trusted.

## Initial template content

- Ship the existing 11 Bambu-derived templates as-is (registered, including PC).
- Add `filament_pctg_template.json` + index entry, authored from the PETG
  template with researched PCTG values (PCTG already exists in the
  `filament_type` enum, `PrintConfig.cpp:2345`).
- **Authoring rule (documented workflow, applies to all future materials):** a
  new template is never written from scratch — clone the nearest material
  cousin's complete template (every key then holds a plausible value), set
  `filament_type`, research-correct the high-signal keys (temps, density,
  volumetric ceiling, cooling, adhesion, thermal length). Unknown types (e.g.
  PEKK) are legal: `filament_type` is an open enum (`f_enum_open`,
  `PrintConfig.cpp:2337`); a template makes the type selectable via A3's
  type harvest.

## Phase 2 (planned): library materialization

Replace the loaded factory filament library with a generated mirror organized
in the template scheme. Builds on v1's plumbing; not part of v1.

- **Generator script** reads the pristine factory tree
  (`resources/profiles/Creality/filament/` — kept byte-identical as the
  generator's *input*, never loaded as presets once phase 2 is active) and
  emits our mirror: material bases at the root (hidden, template-style),
  machine-tuned leaves as their children. **Preset names, `filament_id`s and
  `setting_id`s are preserved verbatim**, so existing user presets, CFS slot
  matching, and old project files resolve unchanged. Effective-value
  equivalence is asserted by scripted resolved-config diff (must be empty).
  Each generated file is stamped with generator version + source provenance.
- **Suppression at load**: the Creality vendor's *filament* section is not
  loaded when the mirror is present and passed its completeness check; machines
  and process profiles load from Creality as today. Policy is expressed in our
  data files (e.g. a supersedes declaration in the mirror's index), not
  hardcoded vendor names in C++. If the mirror is missing or incomplete, the
  factory filaments load as normal (automatic fallback — a broken mirror must
  degrade to stock behavior, never to an empty filament system).
- **Open investigations before implementation**: (1) `ProfileFamilyLoader` /
  Creality printer-management UI reads `Creality.json` filament lists directly —
  verify behavior when those presets aren't loaded; (2) define the
  completeness-check criteria; (3) one end-to-end CFS/sync check with mirrored
  ids.
- Upstream merges remain clean: factory files and `Creality.json` stay
  untouched; after a merge that changes factory filaments, re-run the
  generator, review the diff, re-apply curation.

## Out of scope (explicitly rejected/deferred)

- **Rewiring shipped leaves to inherit from templates in place** — cross-vendor
  inheritance is unsupported by the loader; would touch ~1,200 upstream files;
  permanent merge conflict with upstream. Rejected (phase 2's mirror achieves
  the goal without it).
- **Deleting factory filament files** — forces a `Creality.json` rewrite
  (perpetual upstream conflicts), leaves stale copies loading from
  `%APPDATA%\Sanity\system\` on existing installs anyway, and severs the
  generator's input. Suppression-at-load produces the identical user-visible
  result. Rejected.
- **Factory-chain flatten script for template values** — cut from v1 per YAGNI;
  research curation supersedes it (phase 2's generator is its successor for the
  mirror use case).
- Touching the `fdm_filament_*` layer in any way during v1. It remains the
  shipped library's load-time dedup mechanism until phase 2 supersedes the
  loaded copies.

## Update semantics (for reference)

- Templates: loaded directly from `resources/profiles_template/` → edit file,
  restart, live. No version gate.
- Creality vendor profiles: loaded from `%APPDATA%\Sanity\system\` copies,
  refreshed from `resources/profiles` only on `Creality.json` version bump
  (`PresetBundle.cpp:1715`; install pipeline in `PresetUpdater`/
  `ProfileFamilyLoader`).

## Testing

1. Build `libslic3r_gui` incrementally; full `SanityPrint_app_gui` link before
   app testing.
2. App walkthrough: create a PC filament for K2 0.4, K1 SE 0.4, and the K1C
   from the template seed; confirm sectioned dropdown rendering, delimiter
   unselectability, ASC type ordering, and pre-selected defaults.
3. Restart the app: created filaments load cleanly (no "can not find inherits"),
   editor shows template as parent, modified-vs-parent indicators work.
4. Edit a template value, restart, confirm propagation to the derived filament.
5. Regression: K2 Plus flow still defaults to `Hyper PC`; filament picker shows
   no template entries; config wizard shows no Template vendor.
6. K2 Plus push sync: register a template-derived filament end-to-end
   (printer accepted exotic types in prior testing; confirm once).
7. PCTG: create a PCTG filament from the new template on any printer.

## Divergence accounting

- `CreatePresetsDialog.cpp` — already fork-owned (3rd feature commit); additions.
- `PresetBundle` (or adjacent loader site) — small contained addition for global
  template loading + visibility forcing.
- `GUI_App.cpp` — `migrate_user_filament_presets` reads the behavior data file
  (replaces inline tables added by this fork).
- Everything else is new files or additive data entries.
