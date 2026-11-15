# Base Filaments v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Template vendor a globally loaded, hidden base-filament layer; let the Create Filament dialog seed from templates (with real `inherits` lineage and sectioned dropdowns); externalize the material-behavior tables; fill the Generic PC coverage gaps.

**Architecture:** Spec at `docs/superpowers/specs/2026-06-11-base-filaments-design.md` (Parts A+B only — Phase 2 is NOT in this plan). Template vendor loads directly from `resources/profiles_template/` into the global `PresetBundle` (stays invisible via the existing installed-filaments mechanism — no visibility forcing). All dialog work is in fork-owned `CreatePresetsDialog.cpp`. Data work is JSON files.

**Tech Stack:** C++ (MSVC), wxWidgets, nlohmann::json, CMake (pinned 3.31.12 — see the `build-crealityprint` skill; CLion/Ninja for incremental builds).

**Testing adaptation:** This repo has no usable unit-test infra (UnitTest submodule is on Creality's internal network; tests off by default). TDD steps are replaced by: incremental compile checks (`libslic3r` / `libslic3r_gui` targets), scripted JSON validation, and a final app walkthrough. Every task still ends in a commit.

**Build command used throughout** (from repo root; takes minutes only when headers change):

```powershell
& 'D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe' --build D:\cpbuild\app --config Release --target libslic3r_gui -- -m
```

Expected on success: `... libslic3r_gui.vcxproj -> D:\cpbuild\app\src\slic3r\Release\libslic3r_gui.lib`, exit code 0. (Pre-existing C4828/boost warnings are normal.)

**App-state note for runtime verification:** the app loads system presets from `%APPDATA%\Sanity\system\` (a version-gated copy), but templates load directly from `resources\profiles_template\` — template/data edits under `profiles_template` need only an app restart. Creality-vendor data edits (Task 7) additionally need the `Creality.json` version bump included in that task.

---

### Task 1: Register PC template; author PCTG template (data only)

**Files:**
- Modify: `resources/profiles_template/Template.json`
- Create: `resources/profiles_template/Template/filament/filament_pctg_template.json`

- [ ] **Step 1: Add the two missing `filament_list` entries**

In `resources/profiles_template/Template.json`, the `filament_list` array currently lacks `filament_pc_template` (shipped orphan) and needs the new PCTG entry. Insert after the `filament_pa_template` entry (list is roughly alphabetical):

```json
    {
      "name": "filament_pc_template",
      "sub_path": "filament/filament_pc_template.json"
    },
    {
      "name": "filament_pctg_template",
      "sub_path": "filament/filament_pctg_template.json"
    },
```

- [ ] **Step 2: Create the PCTG template by cloning PETG and patching**

```powershell
Copy-Item 'resources\profiles_template\Template\filament\filament_petg_template.json' 'resources\profiles_template\Template\filament\filament_pctg_template.json'
```

Then edit `filament_pctg_template.json`. First set identity:
- `"name"`: `"Generic PCTG template"`
- `"filament_type"`: `["PCTG"]`

Then apply the curated PCTG values below to every listed key **that exists in the file** (template values are single-element string arrays, e.g. `["260"]`). If a key from this table is absent from the cloned file, skip it and note it in the commit message — do NOT invent new keys:

| key | value | rationale |
|---|---|---|
| `nozzle_temperature` | `["260"]` | PCTG runs ~10–20 °C above PETG |
| `nozzle_temperature_initial_layer` | `["265"]` | |
| `nozzle_temperature_range_low` | `["240"]` | |
| `nozzle_temperature_range_high` | `["280"]` | |
| `hot_plate_temp` | `["75"]` | |
| `hot_plate_temp_initial_layer` | `["80"]` | |
| `eng_plate_temp` | `["75"]` | |
| `eng_plate_temp_initial_layer` | `["80"]` | |
| `textured_plate_temp` | `["75"]` | |
| `textured_plate_temp_initial_layer` | `["80"]` | |
| `filament_density` | `["1.23"]` | |
| `filament_cost` | `["30"]` | |
| `filament_max_volumetric_speed` | `["13"]` | tougher melt than PETG, slightly conservative |
| `temperature_vitrification` | `["76"]` | |

Leave all other keys at their PETG-donor values (cooling, retraction-adjacent, plumbing — per the donor-clone authoring rule in the spec).

- [ ] **Step 3: Validate both JSON files parse and the index matches the folder**

```powershell
Get-Content 'resources\profiles_template\Template.json' | ConvertFrom-Json | Out-Null
Get-Content 'resources\profiles_template\Template\filament\filament_pctg_template.json' | ConvertFrom-Json | Out-Null
$idx = (Get-Content 'resources\profiles_template\Template.json' | ConvertFrom-Json).filament_list.name | Sort-Object
$files = (Get-ChildItem 'resources\profiles_template\Template\filament' -Filter *.json).BaseName | Sort-Object
Compare-Object $idx $files
```

Expected: no parse errors; `Compare-Object` outputs nothing (index and folder agree, now 12 entries each).

- [ ] **Step 4: Commit**

```powershell
git add resources/profiles_template; git commit -m @'
feat: register PC template, add curated PCTG template

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 2: Load the Template vendor globally

**Files:**
- Modify: `src/libslic3r/PresetBundle.cpp` (inside `load_system_presets_from_json`, after the vendor directory loop ends — loop starts at `:1722`; insert before the function's final substitutions/return handling)

- [ ] **Step 1: Add the template load after the system-vendor loop**

The vendor loop iterates `%APPDATA%\Sanity\system\*.json`. Immediately after its closing brace, add:

```cpp
    // SanityPrint: load the Template vendor (printer-agnostic base filaments)
    // directly from resources so template edits go live on restart, bypassing
    // the version-gated data_dir/system copy. The "Template" vendor name is
    // exempted from the instantiation and filament_id gates below, so its
    // non-instantiated bases load as real (hidden) presets that user filament
    // presets can inherit from.
    {
        boost::filesystem::path template_dir =
            (boost::filesystem::path(resources_dir()) / PRESET_PROFILES_TEMOLATE_DIR).make_preferred();
        if (boost::filesystem::exists(template_dir / (std::string(PRESET_TEMPLATE_DIR) + ".json"))) {
            try {
                PresetBundle other;
                append(substitutions,
                       other.load_vendor_configs_from_json(template_dir.string(), PRESET_TEMPLATE_DIR,
                                                           PresetBundle::LoadSystem, compatibility_rule).first);
                std::vector<std::string> duplicates = this->merge_presets(std::move(other));
                for (const std::string &dup : duplicates)
                    BOOST_LOG_TRIVIAL(error) << "Template vendor preset duplicates existing preset: " << dup;
            } catch (const std::exception &err) {
                BOOST_LOG_TRIVIAL(error) << "Failed loading Template vendor: " << err.what();
            }
        }
    }
```

Notes for the implementer:
- `PRESET_PROFILES_TEMOLATE_DIR` ("profiles_template" — typo is upstream's) and `PRESET_TEMPLATE_DIR` ("Template") come from `Preset.hpp:26-27`, already included.
- `resources_dir()` is set in `SanityPrint.cpp:6047` before any preset loading.
- Template filaments have no `filament_id` and `instantiation:"false"` — both already exempted for vendor "Template" at `PresetBundle.cpp:4121` and `:4212`.
- Do NOT add any visibility code: system-preset visibility is governed by `load_installed_filaments` (`PresetBundle.cpp:2017`), which only marks printer-model default materials visible — templates are in no `default_materials` list, so they stay invisible by construction.

- [ ] **Step 2: Build**

Run the build command (header untouched → fast). Expected: exit 0.

- [ ] **Step 3: Runtime smoke check**

Launch `D:\cpbuild\app\src\Release\SanityPrint.exe` with `SLIC3R_RESOURCES_DIR=I:\IdeaProjects\CrealityPrint\resources` (the `.run/SanityPrint.run.xml` config does this). Then:

```powershell
Select-String -Path "$env:APPDATA\Sanity\log\debug_*.log" -Pattern 'got preset Generic PC template' | Select-Object -Last 1
```

Expected: one match (templates loaded). In the app, open the filament dropdown on the prepare page: **no** "template" entries appear.

- [ ] **Step 4: Commit**

```powershell
git add src/libslic3r/PresetBundle.cpp; git commit -m @'
feat: load Template vendor globally from resources (hidden base filaments)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 3: Dialog — collect templates and harvest their types

**Files:**
- Modify: `src/slic3r/GUI/CreatePresetsDialog.hpp` (class `CreateFilamentPresetDialog` members)
- Modify: `src/slic3r/GUI/CreatePresetsDialog.cpp` (`get_all_filament_presets` `:1540`, `clear_filament_preset_map` `:1642`)

- [ ] **Step 1: Add the member**

In `CreateFilamentPresetDialog`'s private section in the .hpp, next to `m_all_presets_map`:

```cpp
    std::unordered_map<std::string, Preset *> m_template_presets; // Template-vendor base filaments, offered for every printer
```

- [ ] **Step 2: Collect template presets in `get_all_filament_presets()`**

At the end of the function (after the global-presets loop ending `:1571`), add:

```cpp
    // Template-vendor base filaments: id-less and invisible, so the loops above
    // skip them. Collect them separately and let them extend the type list —
    // authoring a template for a new material makes that type selectable.
    for (const Preset &preset : preset_bundle->filaments.get_presets()) {
        if (!preset.is_system || !preset.vendor || preset.vendor->id != PRESET_TEMPLATE_DIR) continue;
        auto *filament_types = preset.config.option<ConfigOptionStrings>("filament_type");
        if (filament_types && !filament_types->values.empty())
            m_system_filament_types_set.insert(filament_types->values[0]);
        m_template_presets[preset.name] = new Preset(preset);
    }
```

- [ ] **Step 3: Free them where `m_all_presets_map` is freed**

In `clear_filament_preset_map()` (`:1642`), mirror the existing deletion loop for `m_all_presets_map` with:

```cpp
    for (auto &template_preset : m_template_presets) { delete template_preset.second; }
    m_template_presets.clear();
```

(If the function only clears other maps, add both the delete loop and the clear; match the surrounding style.)

- [ ] **Step 4: Build**

Run the build command. Expected: exit 0.

- [ ] **Step 5: Commit**

```powershell
git add src/slic3r/GUI/CreatePresetsDialog.cpp src/slic3r/GUI/CreatePresetsDialog.hpp; git commit -m @'
feat: create-filament dialog collects template presets and their types

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 4: Dialog — sectioned seed dropdowns

**Files:**
- Modify: `src/slic3r/GUI/CreatePresetsDialog.cpp` — static `create_select_filament_preset_checkbox` (`:369-`), its call site in `get_filament_presets_by_machine` (`:1535`)

- [ ] **Step 1: Extend the helper's signature and switch to index-based selection**

Change the signature (and the `:1535` call site) to:

```cpp
static wxBoxSizer *create_select_filament_preset_checkbox(
    wxWindow *parent,
    std::string &compatible_printer,
    std::vector<Preset *> presets,
    std::vector<Preset *> template_presets,          // NEW: sorted, see step 2
    const std::string &selected_filament_type,       // NEW: current type combo value
    std::unordered_map<::CheckBox *, std::pair<std::string, Preset *>> &machine_filament_preset)
```

Replace the choice-building block (`:444-462`) with a sectioned list plus a parallel row→preset vector (`nullptr` rows are unselectable delimiters):

```cpp
    wxArrayString choices;
    auto row_presets = std::make_shared<std::vector<Preset *>>();
    auto add_row = [&choices, &row_presets](const wxString &label, Preset *preset) {
        choices.Add(label);
        row_presets->push_back(preset);
    };

    add_row(_L("------- Templates -------"), nullptr);
    for (Preset *preset : template_presets)
        add_row(wxString::FromUTF8(preset->name), preset);
    add_row(_L("------- Existing presets -------"), nullptr);
    for (Preset *preset : presets)
        if (preset->is_user()) add_row(wxString::FromUTF8(preset->name), preset);
    for (Preset *preset : presets)
        if (!preset->is_user()) add_row(wxString::FromUTF8(preset->name), preset);
    combobox->Set(choices);

    // Default: first concrete preset matching the selected type; else the
    // type-matching template; else the first selectable row.
    auto type_of = [](Preset *preset) -> std::string {
        auto *types = dynamic_cast<const ConfigOptionStrings *>(preset->config.option("filament_type"));
        return (types && !types->values.empty()) ? types->values[0] : std::string();
    };
    int default_row = -1;
    for (int i = 0; i < (int) row_presets->size(); ++i) {
        Preset *preset = (*row_presets)[i];
        if (!preset) continue;
        bool is_template = i > 0 && i <= (int) template_presets.size();
        if (!is_template && type_of(preset) == selected_filament_type) { default_row = i; break; }
    }
    if (default_row < 0)
        for (int i = 1; i <= (int) template_presets.size(); ++i)
            if (type_of((*row_presets)[i]) == selected_filament_type) { default_row = i; break; }
    if (default_row < 0)
        for (int i = 0; i < (int) row_presets->size(); ++i)
            if ((*row_presets)[i]) { default_row = i; break; }
    combobox->SetSelection(default_row);
    auto last_valid_row = std::make_shared<int>(default_row);
```

Replace both selection handlers' bodies (checkbox `:403-421` and combobox `:430-440`) to resolve by index and veto delimiters — combobox handler:

```cpp
    combobox->Bind(wxEVT_COMBOBOX, [combobox, checkbox, row_presets, last_valid_row, &machine_filament_preset, compatible_printer](wxCommandEvent &e) {
        int row = combobox->GetSelection();
        if (row < 0 || row >= (int) row_presets->size() || (*row_presets)[row] == nullptr) {
            combobox->SetSelection(*last_valid_row); // delimiter rows are not selectable
            return;
        }
        *last_valid_row = row;
        combobox->SetLabelColor(*wxBLACK);
        checkbox->SetValue(true);
        machine_filament_preset[checkbox] = std::make_pair(compatible_printer, (*row_presets)[row]);
        e.Skip();
    });
```

and inside the checkbox handler's `if (value)` branch, replace the name-matching loop with:

```cpp
             int row = combobox->GetSelection();
             if (row >= 0 && row < (int) row_presets->size() && (*row_presets)[row] != nullptr)
                 machine_filament_preset[checkbox] = std::make_pair(compatible_printer, (*row_presets)[row]);
```

(capture `row_presets` in that lambda instead of `presets`).

- [ ] **Step 2: Build the sorted template vector at the call site**

In `get_filament_presets_by_machine()`, before the per-printer loop at `:1532`, build once:

```cpp
    // Templates: same list for every printer, sorted by filament type A→Z, then name.
    std::vector<Preset *> sorted_templates;
    for (auto &template_entry : m_template_presets) sorted_templates.push_back(template_entry.second);
    auto template_type = [](Preset *preset) -> std::string {
        auto *types = dynamic_cast<const ConfigOptionStrings *>(preset->config.option("filament_type"));
        return (types && !types->values.empty()) ? types->values[0] : std::string();
    };
    std::sort(sorted_templates.begin(), sorted_templates.end(), [&template_type](Preset *a, Preset *b) {
        std::string ta = template_type(a), tb = template_type(b);
        return ta != tb ? ta < tb : a->name < b->name;
    });
```

and change the `:1535` call to pass `sorted_templates` and `type_name` (the function's existing local holding the selected type).

- [ ] **Step 3: Build**

Run the build command. Expected: exit 0.

- [ ] **Step 4: Commit**

```powershell
git add src/slic3r/GUI/CreatePresetsDialog.cpp; git commit -m @'
feat: sectioned seed dropdowns with template section (ASC by type)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 5: Dialog — template-seeded presets keep `inherits`

**Files:**
- Modify: `src/slic3r/GUI/CreatePresetsDialog.cpp` — both clone branches in `create_button_item` (`:1262-1319`)

- [ ] **Step 1: Add the inherits override in BOTH branches**

`clone_presets` clears `inherits` (`Preset.cpp:2335`) before applying `dynamic_config` (`:2408`), so an `inherits` key in `dynamic_config` survives and re-links the clone. In each branch, directly after the `seed_material_behavior(dynamic_config, type_name);` line, add:

```cpp
                    // Template seeds are globally resident system presets: keep
                    // the parent link so lineage shows and template tuning
                    // propagates. Concrete-preset seeds stay standalone (cleared
                    // by clone_presets) — unchanged current behavior.
                    if (m_template_presets.count(checked_preset->name))
                        dynamic_config.set_key_value("inherits", new ConfigOptionString(checked_preset->name));
```

- [ ] **Step 2: Build**

Run the build command. Expected: exit 0.

- [ ] **Step 3: Runtime verification (core feature loop)**

Launch the app. Create Filament → type PC → printer rows for K2 Plus 0.4 etc. Pick "Generic PC template" for one printer, create. Then:

```powershell
$f = Get-ChildItem "$env:APPDATA\Sanity\user\*\filament" -Filter '*PC*' | Sort-Object LastWriteTime | Select-Object -Last 1
Select-String -Path $f.FullName -Pattern '"inherits"'
```

Expected: `"inherits": "Generic PC template"`. **Restart the app** — the new filament must load with no "can not find inherits" in the log, and its editor page must show the template as parent.

- [ ] **Step 4: Commit**

```powershell
git add src/slic3r/GUI/CreatePresetsDialog.cpp; git commit -m @'
feat: template-seeded filaments inherit from their template

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 6: Externalize material-behavior tables

**Files:**
- Create: `resources/profiles_template/material_behavior.json`
- Create: `src/libslic3r/MaterialBehavior.hpp`, `src/libslic3r/MaterialBehavior.cpp`
- Modify: `src/libslic3r/CMakeLists.txt` (add the two files to the source list, alphabetical placement)
- Modify: `src/slic3r/GUI/CreatePresetsDialog.cpp` (`seed_material_behavior` lambda `:1224-1258`)
- Modify: `src/slic3r/GUI/GUI_App.cpp` (`migrate_user_filament_presets` `:2919-3006`)
- Modify: `scripts/migrate_builtin_filament_profiles.py`

- [ ] **Step 1: Create the data file** (values transcribed 1:1 from the existing C++ tables)

```json
{
  "comment": "Material behavior seeds, keyed by filament_type. Single source of truth for CreatePresetsDialog, GUI_App migration, and scripts/migrate_builtin_filament_profiles.py.",
  "temp_type": {
    "ABS": 0, "ASA": 0, "PC": 0, "PA": 0, "PA-CF": 0, "PA-GF": 0,
    "PA6-CF": 0, "PET-CF": 0, "PPS": 0, "PPS-CF": 0, "PPA-CF": 0,
    "PPA-GF": 0, "ABS-GF": 0, "ASA-Aero": 0,
    "PLA": 1, "TPU": 1, "PLA-CF": 1, "PLA-AERO": 1, "PVA": 1, "BVOH": 1,
    "HIPS": 2, "PETG": 2, "PE": 2, "PP": 2, "EVA": 2,
    "PE-CF": 2, "PP-CF": 2, "PP-GF": 2, "PHA": 2
  },
  "temp_type_default": 3,
  "bed_adhesion": { "PET": 0.3, "PETG": 0.3, "ABS": 0.1, "ASA": 0.1 },
  "bed_adhesion_default": 0.02,
  "thermal_length": { "ABS": 100, "PA-CF": 100, "PET-CF": 100, "PC": 40, "TPU": 1000 },
  "thermal_length_default": 200,
  "brim_adhesion_coeff": { "PETG": 2, "PCTG": 2, "TPU": 0.5 },
  "brim_adhesion_coeff_default": 1,
  "chamber_temp_limit": { "PLA": 45, "PLA-CF": 45, "PVA": 45, "TPU": 50, "PETG": 55, "PCTG": 55, "PETG-CF": 55 },
  "chamber_temp_limit_default": 0,
  "cooling_smart_zone_types": ["PLA", "PETG", "ABS"],
  "small_island_threshold": { "PETG": 20 },
  "small_island_threshold_default": 10,
  "flexible_types": ["TPU"]
}
```

- [ ] **Step 2: Create the shared loader**

`src/libslic3r/MaterialBehavior.hpp`:

```cpp
#ifndef slic3r_MaterialBehavior_hpp_
#define slic3r_MaterialBehavior_hpp_

#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r {

// Material-behavior seed values, loaded once from
// resources_dir()/profiles_template/material_behavior.json.
// Single source of truth shared by the create-filament dialog and the
// CrealityPrint user-preset migration.
class MaterialBehavior
{
public:
    // Parsed table file; empty json object if the file is missing/broken.
    static const nlohmann::json &tables();

    // Look up `type` in tables()[table_key], falling back to
    // tables()[table_key + "_default"], then to `fallback`.
    static double lookup(const char *table_key, const std::string &type, double fallback);
    static bool   contains(const char *list_key, const std::string &type);
};

} // namespace Slic3r

#endif
```

`src/libslic3r/MaterialBehavior.cpp`:

```cpp
#include "MaterialBehavior.hpp"
#include "Utils.hpp"
#include "libslic3r.h"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {

const nlohmann::json &MaterialBehavior::tables()
{
    static nlohmann::json cached = [] {
        nlohmann::json j = nlohmann::json::object();
        auto path = resources_dir() + "/profiles_template/material_behavior.json";
        try {
            boost::nowide::ifstream file(path);
            if (file.is_open())
                j = nlohmann::json::parse(file);
            else
                BOOST_LOG_TRIVIAL(error) << "material_behavior.json not found: " << path;
        } catch (const std::exception &err) {
            BOOST_LOG_TRIVIAL(error) << "material_behavior.json parse error: " << err.what();
            j = nlohmann::json::object();
        }
        return j;
    }();
    return cached;
}

double MaterialBehavior::lookup(const char *table_key, const std::string &type, double fallback)
{
    const nlohmann::json &j = tables();
    if (j.contains(table_key) && j[table_key].contains(type))
        return j[table_key][type].get<double>();
    std::string default_key = std::string(table_key) + "_default";
    if (j.contains(default_key))
        return j[default_key].get<double>();
    return fallback;
}

bool MaterialBehavior::contains(const char *list_key, const std::string &type)
{
    const nlohmann::json &j = tables();
    if (!j.contains(list_key)) return false;
    for (const auto &entry : j[list_key])
        if (entry.get<std::string>() == type) return true;
    return false;
}

} // namespace Slic3r
```

Add both files to `src/libslic3r/CMakeLists.txt`'s source list (find the alphabetical neighbors, e.g. near `MeshBoolean.cpp`).

- [ ] **Step 3: Rewrite `seed_material_behavior` to use it**

Replace the lambda's body (`CreatePresetsDialog.cpp:1224-1258`) — keep the signature and call sites identical:

```cpp
        auto seed_material_behavior = [](DynamicConfig &cfg, const std::string &ft) {
            cfg.set_key_value("filament_temp_type", new ConfigOptionInts({(int) MaterialBehavior::lookup("temp_type", ft, 3)}));
            cfg.set_key_value("filament_cooling_smart_zone", new ConfigOptionBools({MaterialBehavior::contains("cooling_smart_zone_types", ft)}));
            cfg.set_key_value("filament_bed_adhesion_strength", new ConfigOptionFloats({MaterialBehavior::lookup("bed_adhesion", ft, 0.02)}));
            cfg.set_key_value("filament_thermal_length", new ConfigOptionFloats({MaterialBehavior::lookup("thermal_length", ft, 200.0)}));
            cfg.set_key_value("filament_brim_adhesion_coeff", new ConfigOptionFloats({MaterialBehavior::lookup("brim_adhesion_coeff", ft, 1.0)}));
            cfg.set_key_value("filament_small_island_threshold", new ConfigOptionFloats({MaterialBehavior::lookup("small_island_threshold", ft, 10.0)}));
            cfg.set_key_value("filament_chamber_temp_limit", new ConfigOptionInts({(int) MaterialBehavior::lookup("chamber_temp_limit", ft, 0)}));
            cfg.set_key_value("filament_is_flexible", new ConfigOptionBools({MaterialBehavior::contains("flexible_types", ft)}));
        };
```

Add `#include "libslic3r/MaterialBehavior.hpp"` to the file's include block.

- [ ] **Step 4: Rewrite the migration's tables the same way**

In `GUI_App.cpp`'s `migrate_user_filament_presets` (`:2919`), delete the five `static const std::unordered_map` tables and rewrite the value-derivation section using the same calls (string-array values, matching the existing `put` helper):

```cpp
                auto put_lookup = [&put](const char *json_key, const char *table_key, const std::string &type, double fallback) {
                    const nlohmann::json &tables = MaterialBehavior::tables();
                    if ((tables.contains(table_key) && tables[table_key].contains(type)) ||
                        tables.contains(std::string(table_key) + "_default")) {
                        double value = MaterialBehavior::lookup(table_key, type, fallback);
                        // integral tables serialize without decimals, matching the old literals
                        std::string s = (value == (long) value) ? std::to_string((long) value) : std::to_string(value);
                        s.erase(s.find_last_not_of('0') + 1); if (!s.empty() && s.back() == '.') s.pop_back();
                        put(json_key, s);
                    }
                };
                put_lookup("filament_temp_type", "temp_type", ft, 3);
                put_lookup("filament_bed_adhesion_strength", "bed_adhesion", ft, 0.02);
                put_lookup("filament_thermal_length", "thermal_length", ft, 200);
                put_lookup("filament_brim_adhesion_coeff", "brim_adhesion_coeff", ft, 1);
                put_lookup("filament_chamber_temp_limit", "chamber_temp_limit", ft, 0);
                if (MaterialBehavior::contains("cooling_smart_zone_types", ft)) put("filament_cooling_smart_zone", "1");
                if (ft == "PETG") put("filament_small_island_threshold", "20");
                if (MaterialBehavior::contains("flexible_types", ft)) put("filament_is_flexible", "1");
```

**Behavior-preservation caveat (important):** the old migration only wrote a key when the type was IN a table (no defaults written); `put_lookup` preserves that for `bed_adhesion`/`thermal_length`/`brim_adhesion_coeff`/`chamber_temp_limit` only if their `_default` entries are removed from consideration — to keep semantics identical, for those four call `put` only when `tables[table_key].contains(type)`:

```cpp
                auto put_if_listed = [&put](const char *json_key, const char *table_key, const std::string &type) {
                    const nlohmann::json &tables = MaterialBehavior::tables();
                    if (tables.contains(table_key) && tables[table_key].contains(type)) {
                        double value = tables[table_key][type].get<double>();
                        std::string s = (value == (long) value) ? std::to_string((long) value) : std::to_string(value);
                        s.erase(s.find_last_not_of('0') + 1); if (!s.empty() && s.back() == '.') s.pop_back();
                        put(json_key, s);
                    }
                };
```

Use `put_lookup` (with default) ONLY for `filament_temp_type` (the old code always wrote it via find_or fallback 0? — no: the old migration's `put_mapped` also wrote only when listed). **Final rule: use `put_if_listed` for ALL five tables — identical to old `put_mapped` semantics — and keep the three special-cases as shown.** Add `#include "libslic3r/MaterialBehavior.hpp"` to GUI_App.cpp.

- [ ] **Step 5: Point the Python script at the data file**

In `scripts/migrate_builtin_filament_profiles.py`, locate the five dict literals mirroring these tables and replace them with a single load (path relative to the script):

```python
import json, os
_TABLES = json.load(open(os.path.join(os.path.dirname(__file__), "..", "resources", "profiles_template", "material_behavior.json"), encoding="utf-8"))
TEMP_TYPE = _TABLES["temp_type"]; BED_ADHESION = _TABLES["bed_adhesion"]
THERMAL_LENGTH = _TABLES["thermal_length"]; BRIM_ADHESION = _TABLES["brim_adhesion_coeff"]
CHAMBER_LIMIT = _TABLES["chamber_temp_limit"]
```

(rename references to match the script's existing variable names — read the script first and keep its semantics unchanged).

- [ ] **Step 6: Build (libslic3r changed → larger rebuild) and sanity-diff**

Run the build command (this one rebuilds libslic3r + dependents; expect several minutes). Expected: exit 0.
Sanity: create a PLA filament from a leaf seed in the app; the created JSON's `filament_temp_type` must be `"1"`, `filament_cooling_smart_zone` `"1"` — same as before the refactor.

- [ ] **Step 7: Commit**

```powershell
git add resources/profiles_template/material_behavior.json src/libslic3r/MaterialBehavior.* src/libslic3r/CMakeLists.txt src/slic3r/GUI/CreatePresetsDialog.cpp src/slic3r/GUI/GUI_App.cpp scripts/migrate_builtin_filament_profiles.py
git commit -m @'
refactor: material behavior tables move to one data file

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 7: Generic PC leaves for K2 0.4 and K1 SE 0.4 (+ K1C diagnosis)

**Files:**
- Create: `resources/profiles/Creality/filament/Generic PC @Creality K2 0.4 nozzle.json`
- Create: `resources/profiles/Creality/filament/Generic PC @Creality K1 SE 0.4 nozzle.json`
- Modify: `resources/profiles/Creality.json` (two `filament_list` entries near `:3292`; version bump at `:3`)

(K1C is deliberately out of scope — the template flow covers whatever variant
the user's K1C profile is, per the spec.)

- [ ] **Step 1: Verify the shared-id convention before reusing it**

```powershell
foreach ($f in 'K2 Pro','K1C') { (Get-Content "resources\profiles\Creality\filament\Generic PC @Creality $f 0.4 nozzle.json" | ConvertFrom-Json) | Select-Object name, filament_id, setting_id }
```

Expected: both show the same `filament_id` (K2 Pro's is `00021`). If they differ, use the donor's id for each new file (per-machine-family convention) — do not invent new ids.

- [ ] **Step 2: Create the two files by donor-clone**

```powershell
Copy-Item 'resources\profiles\Creality\filament\Generic PC @Creality K2 Pro 0.4 nozzle.json' 'resources\profiles\Creality\filament\Generic PC @Creality K2 0.4 nozzle.json'
Copy-Item 'resources\profiles\Creality\filament\Generic PC @Creality K1C 0.4 nozzle.json' 'resources\profiles\Creality\filament\Generic PC @Creality K1 SE 0.4 nozzle.json'
```

Edit `Generic PC @Creality K2 0.4 nozzle.json`:
- `"name"`: `"Generic PC @Creality K2 0.4 nozzle"`
- `"compatible_printers"`: `["Creality K2 0.4 nozzle"]` (machine file verified to exist)

Edit `Generic PC @Creality K1 SE 0.4 nozzle.json`:
- `"name"`: `"Generic PC @Creality K1 SE 0.4 nozzle"`
- `"compatible_printers"`: `["Creality K1 SE 0.4 nozzle"]` (machine file verified to exist)
- K1 SE has no chamber heater: set `"activate_chamber_temp_control"`: `"0"` and `"chamber_temperature"`: `"0"` (keys exist in the donor; donor uses scalar strings — keep that style)

Both files keep `"inherits": "fdm_filament_pc"`, `"instantiation": "true"`, donor `filament_id`/`setting_id` (per Step 2), and all tuning values otherwise.

- [ ] **Step 3: Register in `Creality.json` and bump its version**

Insert two entries in `filament_list` adjacent to the existing Generic PC block (`:3292` area), matching the existing format exactly:

```json
        {
            "name": "Generic PC @Creality K2 0.4 nozzle",
            "sub_path": "filament/Generic PC @Creality K2 0.4 nozzle.json"
        },
        {
            "name": "Generic PC @Creality K1 SE 0.4 nozzle",
            "sub_path": "filament/Generic PC @Creality K1 SE 0.4 nozzle.json"
        },
```

Bump line 3: `"version": "25.12.26.17"` → `"version": "25.12.26.18"` (required so existing installs refresh their `%APPDATA%\Sanity\system\` copy).

- [ ] **Step 4: Validate**

```powershell
Get-Content 'resources\profiles\Creality.json' | ConvertFrom-Json | Out-Null
foreach ($n in 'K2','K1 SE') { Get-Content "resources\profiles\Creality\filament\Generic PC @Creality $n 0.4 nozzle.json" | ConvertFrom-Json | Select-Object name, compatible_printers }
```

Expected: parses clean; names/compatible_printers correct.

- [ ] **Step 5: Commit**

```powershell
git add resources/profiles; git commit -m @'
feat: Generic PC presets for K2 0.4 and K1 SE 0.4

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

### Task 8: Full link + acceptance walkthrough

**Files:** none (verification only)

- [ ] **Step 1: Full app link**

```powershell
& 'D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe' --build D:\cpbuild\app --config Release --target SanityPrint_app_gui -- -m
```

Expected: exit 0; fresh `SanityPrint_Slicer.dll` + `SanityPrint.exe` under `D:\cpbuild\app\src\Release\`.

- [ ] **Step 2: Walk the spec's test list** (launch via the run config so `SLIC3R_RESOURCES_DIR` points at the repo)

1. Create Filament → PC: K2 0.4, K1 SE 0.4, K1C rows each offer "Generic PC template" in a Templates section (delimiters unselectable; templates ordered ABS→TPU i.e. type A→Z); K2 0.4 and K1 SE 0.4 now ALSO offer their new `Generic PC @…` concrete presets, pre-selected as the type-matching default.
2. K2 Plus 0.4 regression: default seed is a concrete PC preset (`Hyper PC`/`Generic PC`), NOT the template.
3. Create one filament from a template seed → user JSON has `"inherits": "Generic PC template"`; restart → loads clean, editor shows parent.
4. Edit `filament_pc_template.json` (e.g. `filament_cost` to `"31"`), restart → derived filament's unmodified cost shows 31. Revert the edit after.
5. Filament picker: no "template" entries on any printer. Config wizard / printer management: no Template vendor offered.
6. PCTG: type selectable in Create Filament; create from `Generic PCTG template` on K2 Plus 0.4; slice a calibration cube with it (sanity: no errors; temps 260/75 in gcode header).
7. K2 push sync: register one template-derived filament end-to-end (printer handled exotic types in prior testing; confirm once).

- [ ] **Step 3: Record results**

Append a `## Verification` section with the walkthrough outcomes (pass/fail per item) to this plan file, commit:

```powershell
git add docs/superpowers/plans/2026-06-11-base-filaments-v1.md; git commit -m @'
docs: v1 acceptance walkthrough results

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
'@
```

---

## Verification

**Code-side (complete):**
- All 8 tasks implemented, each per-task spec-reviewed and committed (`58c25bf9`..`e1578589`).
- Integration review found 1 Critical + 2 Important issues, all fixed:
  - Issue 1 (template-derived presets lost filament_id on reload) → dropped the
    `inherits` injection; created filaments are standalone with a minted id
    (`dc38d95a`).
  - Issue 2a (templates visible pre-wizard) → force `is_visible=false` after the
    Template merge (`ac11368d`).
  - Issue 2b (CFS name-fallback force-shows templates) → exclude the Template
    vendor from that fallback (`ac11368d`).
- Per-user-direction id change: user filament ids now mint as 5-char `A-Z0-9`
  (`mint_filament_id`) so they fit the K2/CFS namespace and are RFID-encodable
  (`dc38d95a`).
- Final `SanityPrint_app_gui` link: clean (exit 0). Fixes re-reviewed: all correct.

**GUI walkthrough (pending — needs operator at the keyboard):** see Task 8 Step 2
list. Plus one hardware check: confirm the K2 round-trips a 5-char tag id with a
letter in positions 2-5 (e.g. `A7K2Q`); if firmware restricts those to digits,
narrow the `mint_filament_id` alphabet for positions 2-5.

## Self-review notes (kept for the executor)

- Spec coverage: A1→Task 2, A2→Task 1, A3→Tasks 3+4, inherits-lineage→Task 5, A4→Task 6, B→Task 7, PCTG→Task 1, testing→Task 8. Phase 2: intentionally absent.
- `m_template_presets` is defined in Task 3 and consumed in Tasks 4 (call site) and 5 (inherits check) — same name, same type.
- Task 6 carries a known judgment point (old `put_mapped` wrote only listed types — final rule stated: `put_if_listed` for all five tables). Executor must read the old code once before deleting it.
- Task 4 changes a static helper's signature; the only caller is `:1535`. If other callers exist at implementation time, compile errors will surface them — fix by passing an empty template vector and the current type.
