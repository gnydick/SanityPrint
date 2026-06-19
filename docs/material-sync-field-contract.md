# SanityPrint <-> K2 Plus Material Sync — Field Contract

**Push** = `POST /server/material?<params>` (all data in query string; body is a throwaway `"{}"`, ignore it).
**Pull** = `GET /server/material` (`result.materials[].base` + `kvParam`) — available to API
clients; SanityPrint itself no longer reads it (sync is push-only per
`superpowers/specs/2026-06-10-filament-sync-pushonly-design.md`).

## Identity params (-> `base`)

| Query param | base field | Notes |
|---|---|---|
| `vendor` | `base.brand` | REQUIRED. Slicer sends `filament_vendor`, falls back to `"Custom"` |
| `name` | `base.name` | Upsert match key (tier 2). The preset's display name |
| `id` | `base.id` | Upsert primary key (tier 1). **SanityPrint never sends it** (its pushes are id-less; the printer mints `U####`). Other API clients may send an explicit unused id (**5 chars**). Immutable after creation. Response must always return the row's canonical `result.id` |
| `type` | `base.type` | `filament_type` (e.g. PLA, PETG, TPU, or custom strings) |
| `account` | `userMaterial` marker path | Registering Creality userId. SanityPrint sends its current-login userId, omitting it when logged out. The printer builds the marker **path** itself — `…/usrMaterial/<account>/<name>.json` (real device form: `/mnt/UDISK/creality/userdata/box/usrMaterial/<account>/<materialId>`) — using this `account` as the owner segment, then **echoes that path as `userMaterial` both in `GET /server/material` rows AND per loaded CFS slot in the device status JSON** (`boxsInfo.materialBoxs[].materials[].userMaterial`). The slicer's auto-map gate splits the echoed path on `/` and reads the **second-to-last** segment as the owning userId; it must equal the logged-in account or the slot is rejected for in-slicer mapping. Absent → leave the row's owner untouched. |

## Standard parameter params (-> `kvParam`, keyed by slicer config name)

| Query param | kvParam key | Format |
|---|---|---|
| `color` | `default_filament_colour` | `#RRGGBB` |
| `mintemp` | `nozzle_temperature_range_low` | int |
| `maxtemp` | `nozzle_temperature_range_high` | int |
| `nozzletemp` | `nozzle_temperature` | int |
| `nozzletemp1` | `nozzle_temperature_initial_layer` | int |
| `bedtemp` | `hot_plate_temp` | int |
| `bedtemp1` | `hot_plate_temp_initial_layer` | int |
| `coolplatetemp` | `cool_plate_temp` | int |
| `coolplatetemp1` | `cool_plate_temp_initial_layer` | int |
| `engplatetemp` | `eng_plate_temp` | int |
| `engplatetemp1` | `eng_plate_temp_initial_layer` | int |
| `texturedplatetemp` | `textured_plate_temp` | int |
| `texturedplatetemp1` | `textured_plate_temp_initial_layer` | int |
| `chambertemp` | `chamber_temperature` | int |
| `softeningtemp` | `temperature_vitrification` | int |
| `density` | `filament_density` | float, trailing zeros trimmed |
| `diameter` | `filament_diameter` | float |
| `cost` | `filament_cost` | float |
| `shrink` | `filament_shrink` | float, percent value **without** `%` |
| `flow` | `filament_flow_ratio` | float |
| `maxspeed` | `filament_max_volumetric_speed` | float |
| `soluble` | `filament_soluble` | `"0"` / `"1"` |
| `support` | `filament_is_support` | `"0"` / `"1"` |

## NEW — material behavior params (SanityPrint extension; param name == kvParam key, store verbatim)

| Query param / kvParam key | Format | Meaning |
|---|---|---|
| `filament_temp_type` | int 0-3 | 0=HighTemp, 1=LowTemp, 2=HighLowCompatible, 3=Undefined |
| `filament_cooling_smart_zone` | `"0"`/`"1"` | cooling slowdown smart zone |
| `filament_bed_adhesion_strength` | float (MPa) | bed adhesion yield strength |
| `filament_thermal_length` | float (mm) | thermal expansion characteristic length |
| `filament_brim_adhesion_coeff` | float | brim adhesion multiplier |
| `filament_small_island_threshold` | float (mm^2) | small-island slowdown threshold |
| `filament_chamber_temp_limit` | int (degC) | max safe chamber temp, 0 = no limit |
| `filament_is_flexible` | `"0"`/`"1"` | TPU-like flexible material |

## Per-nozzle calibration params (ONE material, many nozzles)

A material is a single catalog row, but its calibration differs per nozzle. SanityPrint
groups a material's per-nozzle filament presets (same `filament_id`) into ONE push: the
nozzle-independent fields go to flat `kvParam` once, and each nozzle's calibration is sent
as `<slicer_key>@<nozzle>` and stored in `nozzleParam[<nozzle>]` (a sibling of `kvParam`),
with `<nozzle>` joining the row's `nozzleDiameter` array.

| Query param | nozzleParam key | Format |
|---|---|---|
| `pressure_advance@<n>` | `pressure_advance` | float |
| `enable_pressure_advance@<n>` | `enable_pressure_advance` | `"0"`/`"1"` |
| `filament_flow_ratio@<n>` | `filament_flow_ratio` | float |

e.g. `pressure_advance@0.4=0.04`. So flow is now sent per-nozzle (the flat `flow` param is
still accepted for single-nozzle clients). The nozzle is resolved from the preset's
`compatible_printers` → printer `printer_variant`. Buckets merge on partial upsert:
re-syncing one nozzle never clobbers the others.

## Color policy

Colors are authored ONLY in the slicer. The printer stores `default_filament_colour`
verbatim when the `color` param is provided and must NEVER fabricate a value
(no `#FFFFFF` default). When the param is absent, the stored value is left
untouched (absent stays absent). Junk values — empty string and the literal
two-character string `""` — are never stored; the slicer likewise never sends
them. Bare 6-hex values (gcode registrations cannot send `#`) may be
normalized to `#RRGGBB` on either side.

## Rules

1. Any param absent from a request -> leave that kvParam untouched (partial upsert). Per-nozzle buckets merge the same way: re-syncing one nozzle updates `nozzleParam[<that nozzle>]` and leaves the other nozzles untouched.
2. Two-tier upsert key: `id` first (update in place, rename allowed), then `name` (update, return existing id — never overwrite a row's id), else insert (mint `U####` or honor explicit unused id).
3. Response shape on POST: `{"result":{"action":"register|update","brand":...,"id":...,"name":...,"count":N}}` — `id` must always be the row's canonical id. (Informational for SanityPrint, which ignores it; ids are printer-local.)
4. On GET, every `kvParam` key the printer stores can be applied generically by pull-capable API clients (keys are literal slicer config names). SanityPrint does not pull.

## Delete (remove a material)

`DELETE /server/material?id=<id>` — removal is **by id only** (the precise key; no
name/brand bulk delete), non-destructive to every other row, and a timestamped
backup is kept. Symmetric with the `UNREGISTER_MATERIAL ID=…` gcode macro.

SanityPrint's pushes are id-less, so its remove flow first `GET`s the catalog,
matches the target row by `(vendor=brand, name)` to learn its `id`, then `DELETE`s
that id. The slicer only lists/removes rows it registered — those carrying a
non-empty `userMaterial` marker; factory/official rows have no marker and are never
deletable from the slicer.
