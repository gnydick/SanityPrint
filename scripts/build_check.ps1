# Build check run by the machinery git hooks (tiers.fast at commit, tiers.merge at push to main).
#
#   build_check.ps1 <component>...   build only what the touched components need (commit tier)
#   build_check.ps1 -Merge           build SanityPrint_app_gui and run example_tests (push tier)
#
# Components are recorded in .claude/machinery/config.json (`components`). Each maps to one check:
#   libslic3r -> target libslic3r       gui     -> target libslic3r_gui
#   app       -> target SanityPrint_app_gui
#   deps      -> configure deps/ only (a full deps build takes ~8 min; run build_deps.ps1 for that)
#   package   -> compile every package/**/*.ico with rc.exe (catches truncated or old-DIB icons)
#
# The warm build tree is <build root>\check-<checkout name>, so each worktree keeps its own.
# The first run in a new checkout configures and builds from scratch and takes far longer than
# later, incremental runs. Prints a heartbeat every 30 s and ends with one count line:
#   build_check: <failed> of <total> checks failed
param([switch]$Merge, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Components)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CpbuildRoot = if ($env:CPBUILD_ROOT) { $env:CPBUILD_ROOT } elseif (Test-Path 'D:\') { 'D:\cpbuild' } else { 'C:\cpbuild' }

function Fail($msg) { Write-Output "build_check: refused: $msg"; exit 2 }

# Resolve every tool by explicit path.
$cmake = Join-Path $CpbuildRoot 'tools\cmake-3.31.12-windows-x86_64\bin\cmake.exe'
if (-not (Test-Path $cmake)) { Fail "pinned CMake not found at $cmake - install it (see .claude/skills/build-crealityprint/SKILL.md, Step 1)" }
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere not found at $vswhere - install VS Build Tools 2022 (scripts/install_tools.ps1)" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = if ($vsPath) { Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat' } else { '' }
if (-not $vcvars -or -not (Test-Path $vcvars)) { Fail "vcvars64.bat not found under '$vsPath' - install the VC.Tools.x86.x64 component" }
$deps = Join-Path $CpbuildRoot 'OrcaSlicer_dep\usr\local'
if (-not (Test-Path $deps)) { Fail "deps not installed at $deps - run .claude/skills/build-crealityprint/scripts/build_deps.ps1" }

cmd /c "`"$vcvars`" && set" | ForEach-Object { if ($_ -match '^(.*?)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) } }
$rc = (Get-Command rc.exe -ErrorAction SilentlyContinue).Source
if (-not $rc) { Fail 'rc.exe not on the vcvars64 PATH - install a Windows 10/11 SDK' }

$name = Split-Path $RepoRoot -Leaf
$bld = Join-Path $CpbuildRoot "check-$name"
$log = Join-Path $CpbuildRoot "check-$name.log"
Set-Content $log ''

# Run a command with its output in the log and a heartbeat on our own output every 30 s.
function Step($label, $exe, [string[]]$argv) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $exe -ArgumentList $argv -NoNewWindow -PassThru `
        -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
  $null = $p.Handle   # without reading the handle first, ExitCode comes back empty after exit
  while (-not $p.WaitForExit(30000)) { Write-Host ("heartbeat build_check {0} {1}s" -f $label, [int]$sw.Elapsed.TotalSeconds) }
  Get-Content "$log.out", "$log.err" -ErrorAction SilentlyContinue | Add-Content $log
  $ok = ($p.ExitCode -eq 0)
  Write-Host ("build_check: {0} {1} in {2}s" -f $label, $(if ($ok) { 'ok' } else { "FAILED (exit $($p.ExitCode), log $log)" }), [int]$sw.Elapsed.TotalSeconds)
  return $ok
}

function Configure() {
  if (Test-Path (Join-Path $bld 'CMakeCache.txt')) { return $true }
  Step 'configure' $cmake @('-S', "`"$RepoRoot`"", '-B', "`"$bld`"", '-G', '"Visual Studio 17 2022"', '-A', 'x64',
    '-DSLIC3R_BUILD_TESTS=ON', '-DBBL_RELEASE_TO_PUBLIC=1', '-DPROJECT_VERSION_EXTRA=Release',
    "-DCMAKE_PREFIX_PATH=`"$deps`"", '"-DWIN10SDK_PATH=C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0"')
}
function Build($target) {
  (Configure) -and (Step "build $target" $cmake @('--build', "`"$bld`"", '--config', 'Release', '--target', $target, '--', '-m', '/nodeReuse:false'))
}

$targets = @{ libslic3r = 'libslic3r'; gui = 'libslic3r_gui'; app = 'SanityPrint_app_gui' }
$checks = @()
if ($Merge) {
  $checks += { Build 'SanityPrint_app_gui' }
  $checks += { (Build 'example_tests') -and (Step 'ctest example_tests' $ctest @('--test-dir', "`"$bld`"", '-C', 'Release', '-R', '^example_tests$', '--output-on-failure')) }
} else {
  if (-not $Components) { Fail 'no components given - usage: build_check.ps1 <component>... | -Merge' }
  foreach ($c in $Components) {
    switch ($c) {
      { $targets.ContainsKey($_) } { $t = $targets[$c]; $checks += [scriptblock]::Create("Build '$t'"); break }
      'deps' {
        $checks += { Step 'configure deps' $cmake @('-S', "`"$(Join-Path $RepoRoot 'deps')`"", '-B', "`"$bld-deps`"", '-G', '"Visual Studio 17 2022"', '-A', 'x64',
                     "-DDESTDIR=`"$bld-deps-dest`"", '-DCMAKE_BUILD_TYPE=Release', '-DDEP_DEBUG=OFF', '-DDEPS_ARCH=x64') }
        break
      }
      'package' {
        $checks += {
          $icons = Get-ChildItem (Join-Path $RepoRoot 'package') -Recurse -Filter '*.ico'
          $tmp = Join-Path $env:TEMP "build_check_$PID"
          New-Item -ItemType Directory -Force $tmp | Out-Null
          $i = 0; $lines = $icons | ForEach-Object { $i++; "$i ICON `"$($_.FullName -replace '\\', '\\')`"" }
          Set-Content -Encoding ascii (Join-Path $tmp 'icons.rc') ($lines -join "`r`n")
          $ok = Step "rc $($icons.Count) icons" $rc @('/nologo', '/fo', "`"$tmp\icons.res`"", "`"$tmp\icons.rc`"")
          Remove-Item -Recurse -Force $tmp
          $ok
        }
        break
      }
      default { Fail "unknown component '$c' - known: libslic3r, gui, app, deps, package" }
    }
  }
}

$failed = 0
foreach ($check in $checks) { if (-not (& $check)) { $failed++ } }
Write-Output ("build_check: {0} of {1} checks failed" -f $failed, $checks.Count)
if ($failed) { exit 1 }
exit 0
