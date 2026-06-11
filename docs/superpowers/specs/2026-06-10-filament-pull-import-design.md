# Filament Pull Import — Design

**Date:** 2026-06-10
**Area:** `src/slic3r/GUI/FilamentSyncDialog.cpp` (pull path), id minting (push path)
**Companion contract:** `docs/material-sync-field-contract.md`, printer OpenAPI at
`GET /server/material/openapi.json` (interactive docs at `/server/material/docs`)

## Problem

Pull only updates slicer user presets that already exist with the same name. Custom
materials that exist only on the printer are listed in a summary dialog and never
created in the slicer, so new printer filaments never appear. Two defects compound
this:

1. **Classification by id prefix is wrong.** The code treats only `U####`/`P####` ids
   as user-managed. Real catalogs assign arbitrary numeric ids to user materials, so
   most custom rows were classified as stock and ignored.
2. **Slicer-side id minting can collide with catalog ids.** The slicer minted `P1003`
   for a pushed preset; the printer's catalog already had a `P1003` row. Because
   `DELETE /server/material?id=` removes *every* row with that id, the collision later
   caused an unrelated catalog row to be deleted.

Baseline state (after the 2026-06-10 clean-slate wipe): printer catalog holds only the
60 factory rows (brands `Creality` + `Generic`); slicer has zero user filament presets.

## Design

### Custom-row identification

A row is custom **iff `userMaterial` is a non-empty path**. The printer UI cannot
create materials; custom rows are created only through the material API — by this
slicer's push or by external tooling (the API's other clients, e.g. gcode
registration). Every API registration produces a `userMaterial` file (verified live
2026-06-10 with a probe row). Id prefixes are meaningless and must not be used.
(Pre-wipe legacy imports without `userMaterial` no longer exist.)

### Name parsing

Material names may carry a variant suffix: `<base> @<model> <nozzle> nozzle`
(e.g. `Priline PC-CF-GF 1111 @Creality K2 Plus 0.6 nozzle`).

- **base name** = name with the suffix stripped (verbatim if no suffix).
- **nozzle** = parsed from the suffix when present; used only to route per-nozzle
  values (see below).
- The printer's structured `nozzleDiameter` field is always `["0.4"]` (junk default)
  and must be ignored.
- **Model alignment never comes from the suffix.** A material aligns with the model of
  the device it was pulled from, always.

### Pull flow (per selected device, serial)

Pull iterates the selected devices one at a time. For each device:

1. `GET /server/material`; keep rows with non-empty `userMaterial`.
2. Group custom rows by base name.
3. For each base-name group:
   - **Root preset** (named `<base>`, stored as a custom root in `filament/base/`,
     mirroring the Create Filament wizard):
     - Created if absent. Seeded from the system template matching the row's
       `filament_type` (`Generic <type>` family; fall back to any system preset of
       that type). The group's representative values (the suffix-less row if present,
       else the lowest-nozzle row) are applied as the root's settings via the existing
       kvParam normalization (skip empty/`""` values, normalize bare hex colors).
     - `filament_id` = the printer row's id, if that id is free across the union of
       known catalogs; otherwise the row is reconciled first (see *Id model*).
     - If the root already exists (from a previous pull or device), it is updated with
       the same kvParam application, not recreated.
   - **Per-nozzle children** (named `<base> @<model> <nozzle> nozzle`,
     `inherits: <base>`): one per distinct nozzle size among the slicer's printer
     configurations whose `printer_model` matches the source device's model. Each
     child is made compatible with those configurations of its nozzle size
     (wizard-style: compatibility set on the child).
     - A printer row whose suffix names nozzle N provides override values for child N
       (applied on top of the inherited root).
     - Variants with no matching printer row get a bare child (no overrides — pure
       inheritance from the root).
4. Same base name pulled later from a device of a *different* model: reuse the
   existing root, add that model's children under it.
5. Existing same-named user presets keep today's update behavior (kvParam applied,
   placeholder ids adopted).

### Skipping and reporting

- If the source device's model matches no slicer printer configuration, the group is
  skipped and listed by name in the summary.
- The summary reports: roots created, children created, presets updated, rows
  skipped (with reason), devices unreachable.
- After any creation, the preset bundle/sidebar are refreshed so new presets appear
  without restart.

### Id model: the slicer is the sole minter

Ids are five-character catalog keys, and the printer API treats them as the PRIMARY
upsert key, immutable after creation. Verified live against the OpenAPI contract
(`/server/material/openapi.json`): a POST with an explicit unused `id` registers a
new row under exactly that id; `DELETE ?id=` removes **every** row sharing the id.

- **Push always carries a slicer-minted id.** Before minting, the slicer collects the
  union of all ids in every selected printer's catalog (stock and custom alike) plus
  its own presets' filament ids, and mints the first free **`S####`**. `S` is
  SanityPrint's own namespace letter: the printer catalog uses vendor letters
  (`E`=eSUN, `P`=Polymaker, `U`=API mint fallback), so minting under `P` — inherited
  from upstream's unrelated `"P"+md5[0:7]` 8-char scheme — squats Polymaker's space
  and caused the original collision. `S####` keeps the catalog's native 5-char shape
  (registration verified live with a probe row). Because every printer receives the
  same explicit id at registration, the same filament can never acquire different ids
  on different printers.
- **The "adopt the first printer's minted id" convergence logic is deleted** (the
  current `have_canonical` flow in `start_push`). It pushed id-less, let one printer
  mint, and forwarded that id to the rest — which both diverges (name-tier rescues
  silently) and risks tier-1 collisions with unrelated rows on other printers.
- **`U####` rows can still appear** from external tooling registering id-less (the
  API's mint fallback). On pull, such a row's id becomes the root's canonical id if
  it is free across the union; otherwise pull reconciles it immediately.
- **Defensive reconciliation** (same base name, different ids across printers —
  should no longer occur, but external id-less registrations on multiple printers
  could still produce it): the slicer mints a fresh canonical `P####` from the union,
  then per printer holding the filament: `DELETE` the old row, re-`POST` with the
  canonical id and full values. Serial, like the rest of pull. The slicer holds the
  complete preset, so a failure between delete and re-register self-heals on the next
  push; the worst case is a transient gap on one printer, not data loss.

### Implementation notes (reuse, not new plumbing)

- Preset creation reuses the Create Filament wizard machinery: custom roots in
  `filament/base/` and `PresetCollection::clone_presets_for_printer(...)` for the
  per-printer children (`CreatePresetsDialog.cpp` "preset for printer" flow), plus
  `PresetCollection::load_preset(path_from_name(...), ...)` / `Preset::save` for
  batch creation without UI selection churn.
- The fork's hash-based mint in `CreatePresetsDialog.cpp` (`get_filament_id`,
  `"P%04u"` from md5 — only collision-checked against slicer-known ids) is replaced
  by the union-checked `S####` minter; the sync dialog and the wizard should share
  it so wizard-created filaments are also catalog-collision-free.

## Out of scope

- No cross-model fan-out beyond the devices actually pulled from.
- No import-picker UI; pull imports all custom rows.
- Push behavior unchanged except for the minting fix.
- No printer-side API changes.

## Testing

- **Suffix parser:** unit-style coverage for names with/without suffix, multi-word
  models, nozzle sizes (0.2/0.25/0.4/0.6/0.8).
- **End-to-end (manual, against the K2 Plus):**
  1. Register a material via the API id-less (simulating external tooling; printer
     mints `U####`) → pull → root + 0.4/0.6/0.8 children exist, children compatible
     with the matching K2 Plus configurations, values match, root keeps the `U####`.
  2. Pull again → idempotent: no duplicates, updates applied.
  3. Push a slicer preset → row registers under the slicer-minted `P####`
     (`action: register`, same id echoed back); the id is absent from the union
     beforehand and present everywhere afterward.
  4. Register the same name id-less on two printers (forcing divergent `U####`s) →
     pull reconciles: both printers end up with one canonical `P####`.
  5. Material from a device whose model matches no slicer printer → skipped and
     reported.
