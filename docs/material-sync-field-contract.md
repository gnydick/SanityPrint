# SanityPrint <-> K2 Plus Material Sync — Field Contract

**Push** = `POST /server/material?<params>` (all data in query string; body is a throwaway `"{}"`, ignore it).
**Pull** = `GET /server/material` (slicer reads `result.materials[].base` + `kvParam`).

## Identity params (-> `base`)

| Query param | base field | Notes |
|---|---|---|
| `vendor` | `base.brand` | REQUIRED. Slicer sends `filament_vendor`, falls back to `"Custom"` |
| `name` | `base.name` | Upsert match key (tier 2). The preset's display name |
| `id` | `base.id` | Upsert primary key (tier 1). Omitted until the slicer holds a canonical id; **5 chars**; printer auto-mints `U####` when absent. Immutable after creation. Response must always return the row's canonical `result.id` |
| `type` | `base.type` | `filament_type` (e.g. PLA, PETG, TPU, or custom strings) |

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

## Color policy

Colors are authored ONLY in the slicer. The printer stores `default_filament_colour`
verbatim when the `color` param is provided and must NEVER fabricate a value
(no `#FFFFFF` default). When the param is absent, the stored value is left
untouched (absent stays absent). Junk values — empty string and the literal
two-character string `""` — are never stored; the slicer likewise never sends
them. Bare 6-hex values (gcode registrations cannot send `#`) may be
normalized to `#RRGGBB` on either side.

## Rules

1. Any param absent from a request -> leave that kvParam untouched (partial upsert).
2. Two-tier upsert key: `id` first (update in place, rename allowed), then `name` (update, return existing id — never overwrite a row's id), else insert (mint `U####` or honor explicit unused id).
3. Response shape on POST: `{"result":{"action":"register|update","brand":...,"id":...,"name":...,"count":N}}` — `id` must always be the row's canonical id; the slicer adopts it.
4. On GET, every `kvParam` key the printer stores is applied generically by the slicer (keys are literal slicer config names) — so printer-side additions flow to the slicer with zero slicer changes.
