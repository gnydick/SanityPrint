---
name: build-crealityprint
description: >-
  Build CrealityPrint from source on Windows — full toolchain setup, dependency
  build, app build, release versioning, and CLion wiring, including the known
  gotchas (CMake 4.x incompatibility, cross-drive deps, missing ATL, version/update
  channel). Use when building, compiling, rebuilding, or setting up CrealityPrint
  on this machine, or when diagnosing its version / update-channel behavior.
---

# Building CrealityPrint (Windows)

CrealityPrint is a large C++/CMake desktop slicer (lineage: Slic3r → PrusaSlicer →
BambuStudio → OrcaSlicer → CrealityPrint). This skill captures the **complete, working
build process** for this machine, including every non-obvious gotcha discovered the hard
way. Read the **Gotchas** section first — most build failures map to one of them.

Helper scripts live in `scripts/` next to this file. They encode the exact steps below.
They assume this machine's layout (see *Layout*); adjust paths if yours differs.

## Layout (this machine)

| Thing | Path |
|---|---|
| Source repo | `I:\IdeaProjects\CrealityPrint` |
| Build outputs | `D:\cpbuild\` (source is on a small drive; builds go on D:) |
| Pinned CMake 3.31.12 | `D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe` |
| Deps build dir | `D:\cpbuild\deps` |
| Deps install prefix | `D:\cpbuild\OrcaSlicer_dep\usr\local` (→ `CMAKE_PREFIX_PATH`) |
| App build dir | `D:\cpbuild\app` |
| Built exe | `D:\cpbuild\app\src\Release\CrealityPrint.exe` (thin launcher; logic in `CrealityPrint_Slicer.dll`) |

## Gotchas (read this first)

1. **CMake 4.x breaks the whole tree.** winget's CMake (4.3.x) and CLion's bundled CMake
   (4.2.2) both error: *"Compatibility with CMake < 3.5 has been removed"*. The deps tree
   and `src/imgui`, `src/imguizmo` declare `cmake_minimum_required(VERSION 2.8.12 / 3.2)`.
   **Fix: use the pinned CMake 3.31.12** (last 3.x). ExternalProject sub-builds inherit it
   via `CMAKE_COMMAND`, so pinning the top-level cmake fixes everything. In CLion, set the
   toolchain's CMake to this path — do NOT use bundled.

2. **Cross-drive deps build fails at the patch step.** Source on `I:`, build on `D:` →
   `file(RELATIVE_PATH)` in `deps/CMakeLists.txt` returns an *absolute* path → `git apply
   --directory <abs>` fails with `error: invalid path` for OCCT / OpenCV / paho-mqtt.
   **Fix:** the `deps/CMakeLists.txt` cross-drive patch (branch `fix/cross-drive-deps-build`,
   commit `69ad4a2f`): disable the `--directory` flag when `BINARY_DIR_REL` is absolute.
   Alternatively build deps on the *same drive* as the source.

3. **ATL is required (for breakpad).** VS Build Tools' `--includeRecommended` does NOT
   include ATL, so breakpad fails: `Cannot open include file: 'atlbase.h'`. Install the
   `VC.ATL` (+`VC.ATLMFC`) component (`scripts/install_atl.ps1`).

4. **VS Installer quirks.** `setup.exe modify` does **not** accept `--wait` (returns exit
   87 = invalid parameter) — use `--quiet --norestart` and poll for the result. Also
   `Start-Process -ArgumentList @(...)` does NOT quote array elements containing spaces
   (the `--installPath "C:\Program Files..."` gets split → exit 87) — pass a single
   pre-quoted argument string instead.

5. **Internal mirrors are unreachable.** `extract_deps.bat` / `build_deps.bat` pull
   prebuilt deps from `172.20.180.x` (Creality internal) and the `UnitTest` submodule is on
   `172.20.180.12`. Outside Creality's network these fail — **build deps from source.**
   (Tests are off by default, so the `UnitTest` submodule can be skipped.)

6. **MSBuild node reuse holds file locks.** A parallel (`-- -m`) build leaves ~90 idle
   `MSBuild.exe` worker nodes alive ~15 min afterward. Kill them before deleting any dep
   build dirs, or deletes/cleans fail.

7. **Build from a VS developer environment.** Some deps (OpenSSL) build with `perl`+`nmake`,
   so the build scripts import `vcvars64.bat`. Also refresh `PATH` from the registry inside
   scripts — installers update the registry but not this session's inherited `PATH`.

8. **LFS is not actually used.** Despite the README, `.gitattributes` is empty and binary
   tools are committed directly. No `git lfs pull` needed.

## Step 1 — Toolchain (`scripts/install_tools.ps1`, then `scripts/install_atl.ps1`)

Installs via winget (run elevated; one UAC each):
- **VS Build Tools 2022** — `Microsoft.VisualStudio.2022.BuildTools` with
  `--add Microsoft.VisualStudio.Workload.VCTools --add ...VC.Tools.x86.x64
  --add ...Windows11SDK.22621 --add ...VC.CMake.Project --includeRecommended`
- **CMake** (`Kitware.CMake`), **Ninja**, **Strawberry Perl**, **NASM**, **Python 3.12**
- Then **ATL**: `scripts/install_atl.ps1` runs `setup.exe modify --add VC.ATL --add VC.ATLMFC`
- **CLion** (`JetBrains.CLion`) — optional, for IDE builds

After install, **pin CMake 3.31.12** (the installed 4.x will not work):
```powershell
# download once into D:\cpbuild\tools
$rel  = Invoke-RestMethod "https://api.github.com/repos/Kitware/CMake/releases?per_page=100" -Headers @{ 'User-Agent'='cp' }
$v    = $rel | ? { $_.tag_name -match '^v3\.31\.\d+$' } | Sort-Object { [version]($_.tag_name.TrimStart('v')) } -Desc | Select -First 1
$asset= $v.assets | ? { $_.name -match 'windows-x86_64\.zip$' }
Invoke-WebRequest $asset.browser_download_url -OutFile D:\cpbuild\cm.zip
Expand-Archive D:\cpbuild\cm.zip D:\cpbuild\tools -Force; Remove-Item D:\cpbuild\cm.zip
```

Verify: `cl.exe` exists under the BuildTools MSVC dir, `atlmfc\include\atlbase.h` exists,
Windows SDK `10.0.22621.0` present, and the pinned `cmake.exe --version` says `3.31.12`.

## Step 2 — Dependencies (`scripts/build_deps.ps1`) — ~30 min

Builds ~30 deps (Boost, wxWidgets, OpenCV, OCCT, OpenVDB, TBB, CGAL, OpenSSL, FFmpeg,
breakpad, …) from source and installs to `D:\cpbuild\OrcaSlicer_dep\usr\local`.

The script: refreshes PATH → prepends pinned CMake 3.31 → imports `vcvars64` → then:
```
cmake -S <repo>\deps -B D:\cpbuild\deps -G "Visual Studio 17 2022" -A x64 `
      -DDESTDIR=D:\cpbuild\OrcaSlicer_dep -DCMAKE_BUILD_TYPE=Release -DDEP_DEBUG=OFF -DDEPS_ARCH=x64
cmake --build D:\cpbuild\deps --config Release --target deps -- -m
```
Requires the **cross-drive patch** (Gotcha 2) and **ATL** (Gotcha 3). If a single dep
fails under `-m`, it can interrupt sibling downloads (collateral "invalid path" errors) —
kill workers (Gotcha 6), delete the incomplete `dep_<name>-prefix` dirs, and rebuild just
those targets (`cmake --build D:\cpbuild\deps --target dep_OCCT dep_OpenCV ...`).

## Step 3 — App (`scripts/build_app.ps1`) — builds `ALL_BUILD`

```
$env:BUILD_ID = '4472'   # release build number (see Versioning)
cmake -S <repo> -B D:\cpbuild\app -G "Visual Studio 17 2022" -A x64 `
      -U CREALITYPRINT_VERSION `
      -DBBL_RELEASE_TO_PUBLIC=1 -DPROJECT_VERSION_EXTRA=Release `
      -DCMAKE_PREFIX_PATH=D:\cpbuild\OrcaSlicer_dep\usr\local -DCMAKE_BUILD_TYPE=Release `
      -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0"
cmake --build D:\cpbuild\app --config Release --target ALL_BUILD -- -m
```

## Versioning & update channel (important — not just cosmetic)

`version.inc` is a **placeholder** that hardcodes `CREALITYPRINT_VERSION "7.0.0"` for *every*
7.x release tag. Creality's private CI rewrites it "from the git label" (per the file's own
header comment). The public GitHub repo is a **squashed snapshot** (~46 commits total), so
the internal build number (e.g. `4472`) is **not** in the source and cannot be derived.

The committed fix (in `version.inc`) does what the CI does, properly:
- Derive `X.Y.Z` from `git describe --tags --abbrev=0` (e.g. `v7.1.1` → `7.1.1`).
- Append the build number from `$ENV{BUILD_ID}` (the missing CI counter) → `7.1.1.4472`.
- Pass `-U CREALITYPRINT_VERSION` on configure: a prior `-D` override caches the value
  (e.g. a mangled `7`) and silently **blocks** the derivation. `-U` clears it.

The app renders `"V" + CREALITYPRINT_VERSION` → **V7.1.1.4472**.

**`PROJECT_VERSION_EXTRA` selects the cloud/update server** (`GUI.cpp get_cloud_api_url`):
- `Alpha`/`Dev` → `admin-pre.crealitycloud.com` (pre-release server; reports a stale older
  "latest", causing a spurious *downgrade* update nag) and shows an `Alpha` suffix.
- `Release` → `api.crealitycloud.com` (production; correct latest) and **no suffix**.

So **always build releases with `-DPROJECT_VERSION_EXTRA=Release`** — it fixes the suffix
AND the bogus update prompt in one shot. The default is `Alpha`.

## Step 4 — CLion

CLion's bundled CMake is 4.2.2 (broken, Gotcha 1). One-time setup:
1. Settings → Build, Execution, Deployment → **Toolchains** → Visual Studio, architecture
   `amd64`, and set **CMake** to `D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe`.
2. A ready CLion CMake profile is in `scripts/clion-cmake.xml` — copy it to `.idea/cmake.xml`
   (`.idea/` is gitignored, so it can't be committed there). The committed
   `.run/CrealityPrint.run.xml` sets `SLIC3R_RESOURCES_DIR` so Run works out of the box.

Note: plain **IntelliJ IDEA cannot build C++/CMake** — use **CLion**.

## Running

Set `SLIC3R_RESOURCES_DIR=<repo>\resources` (the run config does this), then launch
`D:\cpbuild\app\src\Release\CrealityPrint.exe`.

## Quick verification

- Deps done: 30 `*-done` stamps under `D:\cpbuild\deps\*-prefix\src\*-stamp`.
- App version: `D:\cpbuild\app\src\libslic3r\libslic3r_version.h` →
  `CREALITYPRINT_VERSION "7.1.1.4472"`, `PROJECT_VERSION_EXTRA "Release"`.
- Output: `CrealityPrint_Slicer.dll` (~73 MB) + `CrealityPrint.exe` under `…\src\Release\`.
