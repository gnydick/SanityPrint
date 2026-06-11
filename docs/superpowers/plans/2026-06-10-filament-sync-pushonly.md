# Filament Sync Push-Only Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make filament sync push-only and id-less: printers mint their own catalog ids, the slicer never sends/stores/adopts them, and the pull path is removed.

**Architecture:** All changes are deletions/simplifications in fork-only feature code (`FilamentSyncDialog.cpp/.hpp`) plus one revert-to-upstream in `CreatePresetsDialog.cpp`. No new files, no API changes. Spec: `docs/superpowers/specs/2026-06-10-filament-sync-pushonly-design.md`.

**Tech Stack:** C++ (wxWidgets GUI, boost::thread, libcurl via `Slic3r::Http`), CMake build (pinned CMake 3.31.12), live verification against the K2 Plus material REST API at `http://192.168.0.130:7125/server/material`.

**Build command (used by every task):**

```powershell
& D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe --build D:\cpbuild\app --config Release --target ALL_BUILD -- -m
```

Expected: exit code 0, `SanityPrint_Slicer.dll` relinked. (No unit-test framework is wired in this tree — `SLIC3R_BUILD_TESTS` is OFF and the UnitTest submodule lives on an unreachable internal server — so each task verifies by compile + the live-API task at the end.)

---

### Task 1: Stop sending `id` in push payloads

**Files:**
- Modify: `src/slic3r/GUI/FilamentSyncDialog.cpp` (in `payload_from_preset`, ~line 113)

- [ ] **Step 1: Delete the id emission block**

In `payload_from_preset`, delete this block (keep the `p.emplace_back("name", ...)` line above it and the `add_string_param(cfg, p, "type", ...)` line below it):

```cpp
    // Only canonical ids (printer-minted, previously adopted) are sent, so all
    // printers converge on one id. Slicer placeholder ids (P-prefixed) are
    // omitted: the printer mints U#### and start_sync adopts it from the
    // upsert response.
    if (!preset.filament_id.empty() && preset.filament_id != "null" && preset.filament_id.front() != 'P')
        p.emplace_back("id", preset.filament_id);
```

- [ ] **Step 2: Build**

Run the build command. Expected: success (nothing else references the removed block).

- [ ] **Step 3: Commit**

```powershell
git add src/slic3r/GUI/FilamentSyncDialog.cpp
git commit -m "feat: push filaments id-less; printers own their catalog ids"
```

---

### Task 2: Remove id adoption/convergence from the push flow

**Files:**
- Modify: `src/slic3r/GUI/FilamentSyncDialog.cpp` (`post_material` ~line 376, `start_sync` ~line 416)

- [ ] **Step 1: Simplify `post_material` to return success only**

Replace the whole function (including its doc comment, currently "POST one material to one device. Returns the canonical id from the response (empty on failure).") with:

```cpp
// POST one material to one device. Returns true on a 200 response.
static bool post_material(const SyncDevice &dev, const std::vector<std::pair<std::string, std::string>> &params,
                          const std::string &preset_name)
{
    std::string url = "http://" + dev.address + ":" + std::to_string(MATERIAL_API_PORT) + "/server/material";
    char        sep = '?';
    for (const auto &kv : params) {
        url += sep + kv.first + "=" + Http::url_encode(kv.second);
        sep = '&';
    }

    bool ok   = false;
    Http http = Http::post(url);
    // A non-empty body is required: Http::priv::http_perform installs a file
    // read callback unconditionally, and a body-less POST makes libcurl invoke
    // it with a garbage userp (hard crash). All request data is in the query
    // string; the printer ignores the body.
    http.set_post_body(std::string("{}"));
    http.timeout_connect(5)
        .timeout_max(15)
        .on_complete([&ok](std::string /*body*/, unsigned status) { ok = (status == 200); })
        .on_error([&preset_name, &dev](std::string /*body*/, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: POST failed for '" << preset_name << "' on " << dev.address
                                     << " status=" << status << " error=" << error;
        })
        .perform_sync();
    return ok;
}
```

- [ ] **Step 2: Simplify `start_sync`**

Replace the whole `FilamentSyncDialog::start_sync` function body with:

```cpp
void FilamentSyncDialog::start_sync(const std::vector<SyncDevice> &targets)
{
    auto payloads = std::make_shared<std::vector<MaterialPayload>>(collect_custom_filament_payloads());
    auto devices  = std::make_shared<std::vector<SyncDevice>>(targets);

    boost::thread([payloads, devices]() {
        int         ok_count   = 0;
        int         fail_count = 0;
        std::string failures;

        try {
            for (const MaterialPayload &payload : *payloads) {
                for (const SyncDevice &dev : *devices) {
                    if (post_material(dev, payload.params, payload.preset_name)) {
                        ++ok_count;
                    } else {
                        ++fail_count;
                        if (failures.size() < 600)
                            failures += "\n" + payload.preset_name + " -> " + dev.name;
                    }
                }
            }
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: push thread exception: " << e.what();
        }

        wxGetApp().CallAfter([ok_count, fail_count, failures]() {
            wxString msg = wxString::Format(_L("Filament sync finished: %d pushed, %d failed."), ok_count, fail_count);
            if (fail_count > 0)
                msg += "\n" + from_u8(failures);
            MessageDialog dlg(wxGetApp().mainframe, msg, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Sync Filaments"),
                              wxOK | wxCENTRE);
            dlg.ShowModal();
        });
    }).detach();
}
```

This deletes: the `reconciled` map, the `have_canonical` / `id_it` `std::find_if` logic, the "adopt the printer-minted id" comment + `payload.params.emplace_back("id", got_id)`, and the `CallAfter` pass that wrote adopted ids into presets (`bundle->filaments.find_preset` / `preset->filament_id = ...` / `preset->save`) along with its "%d filament(s) adopted printer-assigned ids." message line.

Note: `is_placeholder_id` (~line 369) is still referenced by `start_pull` at this point — it is deleted in Task 3, not here.

- [ ] **Step 3: Build**

Run the build command. Expected: success. If `<algorithm>` (for the removed `std::find_if`) is flagged unused — it is NOT removed; `std::min` in the dialog ctor still needs it.

- [ ] **Step 4: Commit**

```powershell
git add src/slic3r/GUI/FilamentSyncDialog.cpp
git commit -m "feat: drop printer-id adoption; push reports success/failure only"
```

---

### Task 3: Remove the pull path and its UI

**Files:**
- Modify: `src/slic3r/GUI/FilamentSyncDialog.cpp` (ctor ~lines 313-318 and 341-347, `is_placeholder_id` ~line 369, `start_pull` ~lines 489-596)
- Modify: `src/slic3r/GUI/FilamentSyncDialog.hpp` (lines 37, 43)

- [ ] **Step 1: Delete the Pull button creation in the ctor**

```cpp
    m_pull_button = new Button(this, _L("Pull"));
    m_pull_button->SetBackgroundColor(btn_bg_blue);
    m_pull_button->SetTextColor(wxColour("#FFFFFE"));
    m_pull_button->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_pull_button->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(m_pull_button, 0, wxRIGHT, FromDIP(10));
```

- [ ] **Step 2: Delete the Pull button binding in the ctor**

```cpp
    m_pull_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        std::vector<SyncDevice> targets = selected_targets(true);
        if (targets.empty())
            return;
        start_pull(targets);
        EndModal(wxID_OK);
    });
```

- [ ] **Step 3: Delete the whole `FilamentSyncDialog::start_pull` function**

It is the last function in the file, from `void FilamentSyncDialog::start_pull(const std::vector<SyncDevice> &targets)` down to its closing `}` just before the final `}} // namespace Slic3r::GUI`.

- [ ] **Step 4: Delete `is_placeholder_id` (unreferenced once start_pull is gone)**

```cpp
// A slicer-minted id (P-prefixed) is a placeholder; canonical ids are minted by
// the printer (U####) and adopted by the slicer on first successful push.
static bool is_placeholder_id(const std::string &id)
{
    return id.empty() || id == "null" || id.front() == 'P';
}
```

- [ ] **Step 5: Trim the stale Pull reference in the kvParam comment**

In `payload_from_preset` (~line 147), replace:

```cpp
    // Parameterized material behavior fields (SanityPrint extension). The param
    // names are the literal slicer config keys; the printer stores them verbatim
    // in kvParam, which makes Pull apply them generically with no further mapping.
```

with:

```cpp
    // Parameterized material behavior fields (SanityPrint extension). The param
    // names are the literal slicer config keys; the printer stores them verbatim
    // in kvParam.
```

- [ ] **Step 6: Remove the two pull members from the header**

In `FilamentSyncDialog.hpp` delete:

```cpp
    void start_pull(const std::vector<SyncDevice> &targets);
```

and

```cpp
    Button   *m_pull_button = nullptr;
```

- [ ] **Step 7: Static payload audit**

```powershell
Select-String -Path src\slic3r\GUI\FilamentSyncDialog.cpp -Pattern 'emplace_back\("id"'
```

Expected: zero matches — no code path can put an `id` param into a push request (this is the spec's "payload audit" check, and it also makes the spec's two-printer test structurally moot: with no id emission and no id forwarding left anywhere, ids cannot travel between printers).

- [ ] **Step 8: Build**

Run the build command. Expected: success; any leftover reference to `start_pull`, `m_pull_button`, or `is_placeholder_id` is a compile error — fix by deleting the leftover, not by re-adding the symbol.

- [ ] **Step 9: Commit**

```powershell
git add src/slic3r/GUI/FilamentSyncDialog.cpp src/slic3r/GUI/FilamentSyncDialog.hpp
git commit -m "feat: remove filament pull; sync is push-only"
```

---

### Task 4: Revert the wizard id mint to upstream's scheme

**Files:**
- Modify: `src/slic3r/GUI/CreatePresetsDialog.cpp` (`get_filament_id`, ~lines 591-616)

The fork shortened upstream's mint to 5 chars solely so ids fit the printer catalog; with ids out of the sync, upstream parity wins (divergence policy). The replacement text below is byte-identical to upstream `v7.1.1` (verify with `git show v7.1.1:src/slic3r/GUI/CreatePresetsDialog.cpp`).

- [ ] **Step 1: Replace the mint lambda with upstream's expression**

Replace:

```cpp
    // Mint a 5-character id ("P" + 4 decimal digits) to match the K2/CFS
    // ecosystem material-code convention: material_database.json ids and CFS
    // RFID material codes are 5 characters (vendor letter + 4 digits), and the
    // sync feature carries this id onto the printer verbatim.
    auto mint_id = [](const std::string &seed) {
        unsigned long h = std::stoul(calculate_md5(seed).substr(0, 7), nullptr, 16);
        char buf[6];
        snprintf(buf, sizeof(buf), "P%04lu", h % 10000);
        return std::string(buf);
    };
    std::string user_filament_id = mint_id(vendor_typr_serial);
```

with:

```cpp
    std::string user_filament_id = "P" + calculate_md5(vendor_typr_serial).substr(0, 7);
```

- [ ] **Step 2: Replace the retry mint with upstream's expression**

Replace:

```cpp
            user_filament_id = mint_id(vendor_typr_serial + get_curr_time());
```

with:

```cpp
            user_filament_id = "P" + calculate_md5(vendor_typr_serial + get_curr_time()).substr(0, 7);
```

- [ ] **Step 3: Verify the function now matches upstream**

```powershell
git show v7.1.1:src/slic3r/GUI/CreatePresetsDialog.cpp > $env:TEMP\upstream_cpd.cpp
# upstream uses crealityprint_* names elsewhere in the file; compare just this function's mint lines
Select-String -Path $env:TEMP\upstream_cpd.cpp -Pattern 'user_filament_id = "P"' | ForEach-Object Line
Select-String -Path src\slic3r\GUI\CreatePresetsDialog.cpp -Pattern 'user_filament_id = "P"' | ForEach-Object Line
```

Expected: the two pairs of lines are identical.

- [ ] **Step 4: Build**

Run the build command. Expected: success.

- [ ] **Step 5: Commit**

```powershell
git add src/slic3r/GUI/CreatePresetsDialog.cpp
git commit -m "revert: restore upstream 8-char filament id mint (ids no longer synced)"
```

---

### Task 5: Live verification against the K2 Plus

**Files:** none (verification only). Requires the freshly built app and the printer online at `192.168.0.130`.

- [ ] **Step 1: Record the catalog baseline**

```powershell
(Invoke-RestMethod 'http://192.168.0.130:7125/server/material' -TimeoutSec 10).result.count
```

Expected: `60` (post-wipe baseline; if different, note the value — the deltas below are what matter).

- [ ] **Step 2 (manual, in the app): create a test filament and push it**

1. Launch `D:\cpbuild\app\src\Release\SanityPrint.exe`.
2. Create a filament via the wizard (sidebar filament dropdown → Create Filament): vendor `SyncTest`, type `PLA`, name serial `Plan Probe` (any distinct name works — note the resulting preset name).
3. Open the sync dialog (Sync filaments action in the prepare screen's action strip). **Verify the dialog shows only Push and Cancel — no Pull button.**
4. Select the K2 Plus, click Push. Expected summary: `Filament sync finished: N pushed, 0 failed.` with no "adopted printer-assigned ids" line (that message no longer exists).

- [ ] **Step 3: Verify the printer row is printer-minted and values arrived**

```powershell
$r = (Invoke-RestMethod 'http://192.168.0.130:7125/server/material' -TimeoutSec 10).result
$r.count
$row = $r.materials | Where-Object { $_.base.brand -eq 'SyncTest' }
"id=$($row.base.id)  name=$($row.base.name)  userMaterial set: $([bool]$row.userMaterial)"
"nozzletemp=$($row.kvParam.nozzle_temperature)  type=$($row.base.type)  density=$($row.kvParam.filament_density)"
```

Expected: count = baseline + (number of pushed presets); the SyncTest row's `id` starts with `U` (printer-minted — proving no id was sent); `userMaterial` is set; the kvParam values match what the wizard preset holds (e.g. nozzle temperature). (`action: register` itself is no longer observable through the app — `post_material` discards the response body — so registration is verified by the row's existence here.)

- [ ] **Step 4: Verify upsert idempotence and value update**

In the app, edit the test filament (change nozzle temperature by exactly +5°), save, push again to the same printer.

```powershell
$r = (Invoke-RestMethod 'http://192.168.0.130:7125/server/material' -TimeoutSec 10).result
$r.count
$row = $r.materials | Where-Object { $_.base.brand -eq 'SyncTest' }
"rows=$(@($row).Count)  id=$($row.base.id)  nozzletemp=$($row.kvParam.nozzle_temperature)"
```

Expected: same count as Step 3 (no duplicate row), exactly 1 SyncTest row, **same `U####` id as Step 3**, and `nozzletemp` is Step 3's value + 5 (the update reached the printer through the name-tier upsert).

- [ ] **Step 5: Verify the slicer preset kept its own id**

```powershell
$f = Get-ChildItem "$env:APPDATA\Sanity\Sanity Print\7.0\user\*\filament" -Recurse -Filter '*Plan Probe*'
Select-String -Path $f.FullName -Pattern '"filament_id"' | ForEach-Object Line
```

Expected: an 8-char `P`+hex id (upstream scheme, from Task 4) — NOT a `U####` (proving adoption is gone).

- [ ] **Step 6: Clean up**

```powershell
$r = (Invoke-RestMethod 'http://192.168.0.130:7125/server/material' -TimeoutSec 10).result
$id = ($r.materials | Where-Object { $_.base.brand -eq 'SyncTest' }).base.id
Invoke-RestMethod -Method Delete -Uri "http://192.168.0.130:7125/server/material?id=$id" -TimeoutSec 10 | Out-Null
(Invoke-RestMethod 'http://192.168.0.130:7125/server/material' -TimeoutSec 10).result.count
```

Expected: count back to baseline. Then delete the test preset in the app (filament dropdown → delete preset), close the app.

- [ ] **Step 7: Final commit check**

```powershell
git log --oneline -5
git status --short
```

Expected: the four feature commits from Tasks 1-4 present, clean tree (no stray edits).
