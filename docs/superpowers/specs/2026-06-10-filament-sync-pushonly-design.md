# Filament Sync — Push-Only Simplification

**Date:** 2026-06-10
**Area:** `src/slic3r/GUI/FilamentSyncDialog.cpp`, `CreatePresetsDialog.cpp` (id mint)
**Companion contract:** `docs/material-sync-field-contract.md`, printer OpenAPI at
`GET /server/material/openapi.json` (interactive docs at `/server/material/docs`)
**Supersedes:** the 2026-06-10 pull-import design (deleted in the same commit).

## Decision

The slicer is the **single author** of filament presets; printers are passive
recipients. Sync is **push-only** and **id-less**. This dissolves every problem the
pull-import design existed to solve: custom-row classification, per-model preset
fan-out, id namespace collisions, cross-printer id divergence, and reconciliation.

Explored and rejected on the way here (kept for the record):

- *Pull-import with custom root + per-nozzle children* — viable (upstream's
  `clone_presets_for_printer` machinery supports it) but adds a parallel authoring
  path that must answer classification, model-alignment, and id questions forever.
- *Slicer as sole id minter (`S####`, union-checked)* — collision-proof, but solves a
  problem that only exists because ids were being synced at all.
- *Per-device id maps* — correct but heavy bookkeeping.

## Design

### Push (the only sync direction)

For each selected device, for each selected user filament preset:

- `POST /server/material?vendor=...&name=...&<params>` — **never an `id` param**.
- The printer's two-tier upsert reduces to its name tier: same name → row updated in
  place (its id never changes); new name → row inserted, printer mints its own
  `U####`.
- Ids are **printer-local implementation details**. The slicer does not send, store,
  adopt, or display them. Two printers holding the same filament under different ids
  is correct and invisible.

### How new filaments reach the slicer

Through the slicer only: the Create Filament wizard (root + per-printer children),
then push. Materials registered on printers by external tooling stay printer-local
by design; the slicer never reads the catalog back.

### Code removals (all in fork feature code)

- `FilamentSyncDialog.cpp`: the `id` param emission in `payload_from_preset`
  (the `preset.filament_id` check), the `have_canonical` / `reconciled` id-adoption
  flow in `start_sync` and its `CallAfter` persistence pass, `is_placeholder_id`,
  and the entire pull path (`start_pull`, the Pull button/choice in the dialog UI).
- `CreatePresetsDialog.cpp`: revert the fork's 5-char `"P%04u"` mint in
  `get_filament_id` to upstream v7.1.1's `"P" + md5(name).substr(0, 7)`. The 5-char
  constraint existed only to fit ids into the printer catalog; with ids out of the
  sync, upstream parity wins (divergence policy).

### What stays

- The push parameter mapping (field contract: identity, standard, and the 8
  material-behavior extension params) is unchanged.
- kvParam value hygiene on push (no empty values, no `""` junk, `#RRGGBB` colors)
  is unchanged.
- The printer API keeps its full surface (GET/POST/DELETE, id minting); other
  clients may use it freely — the slicer just stops consuming GET.

## Out of scope

- Any pull/import path. If reading printer catalogs back ever becomes wanted, the
  superseded pull-import design (git history of this file's predecessor) documents
  the issues it must answer.
- Printer-side API changes.

## Testing

- **Push new preset** → printer row registered (`action: register`), printer-minted
  id, values correct, `userMaterial` file present.
- **Push same preset again** (values changed) → same row updated
  (`action: update`), id unchanged, no duplicate row.
- **Push to two printers** → each mints its own id; both rows carry identical
  name/values; no id is forwarded between them.
- **Payload audit** → no push request ever contains an `id` param.
- **Wizard regression** → creating a filament via the wizard still produces root +
  children with the reverted 8-char id scheme; push of those presets works id-less.
