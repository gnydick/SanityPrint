$ErrorActionPreference = 'Continue'
# Portable roots: repo derived from this script's path; build root from $env:CPBUILD_ROOT,
# else D:\cpbuild when a D: drive exists, else C:\cpbuild (single-drive machine).
$RepoRoot    = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$CpbuildRoot = if ($env:CPBUILD_ROOT) { $env:CPBUILD_ROOT } elseif (Test-Path 'D:\') { 'D:\cpbuild' } else { 'C:\cpbuild' }
New-Item -ItemType Directory -Force $CpbuildRoot | Out-Null
$log  = "$CpbuildRoot\deps_build.log"
$done = "$CpbuildRoot\deps.done"
Remove-Item $done -ErrorAction SilentlyContinue
function Log($m){ $ts=(Get-Date).ToString('HH:mm:ss'); Add-Content -Path $log -Value "$ts  $m" }

Log "########## DEPS BUILD START ##########"

# 1. Refresh PATH from registry (installers updated it after this session's parent started)
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
foreach($d in @('C:\Strawberry\perl\bin','C:\Strawberry\c\bin','C:\Program Files\NASM')){
  if((Test-Path $d) -and ($env:Path -notlike "*$d*")){ $env:Path = "$d;" + $env:Path }
}
# Pin CMake 3.31.x: installed CMake 4.x errors on cmake_minimum_required(<3.5) used across this tree.
# ExternalProject sub-builds inherit this cmake via CMAKE_COMMAND, so pinning the top level fixes all of them.
$cmakeBin = "$CpbuildRoot\tools\cmake-3.31.12-windows-x86_64\bin"
$env:Path = "$cmakeBin;" + $env:Path
$cmake = Join-Path $cmakeBin 'cmake.exe'

# 2. Import VS developer environment (brings cl, nmake, link, INCLUDE, LIB, latest Win SDK)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if(-not (Test-Path $vswhere)){ $vswhere = Join-Path $RepoRoot 'tools\vswhere.exe' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Log "VS install: $vsPath"
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if(-not (Test-Path $vcvars)){ Log "FATAL: vcvars64 not found at $vcvars"; Set-Content $done 'fail-no-vcvars'; exit 1 }
cmd /c "`"$vcvars`" && set" | ForEach-Object { if($_ -match '^(.*?)=(.*)$'){ [Environment]::SetEnvironmentVariable($matches[1],$matches[2]) } }
Log ("cl    = " + (Get-Command cl    -ErrorAction SilentlyContinue).Source)
Log ("cmake = " + (Get-Command cmake -ErrorAction SilentlyContinue).Source)
Log ("perl  = " + (Get-Command perl  -ErrorAction SilentlyContinue).Source)
Log ("nmake = " + (Get-Command nmake -ErrorAction SilentlyContinue).Source)

# 3. Configure deps
$src = Join-Path $RepoRoot 'deps'; $bld = "$CpbuildRoot\deps"; $dest = "$CpbuildRoot\OrcaSlicer_dep"
# Cross-drive patch workaround only needed when source and build are on different drives.
$crossDrive = if ((Split-Path $RepoRoot -Qualifier) -ieq (Split-Path $CpbuildRoot -Qualifier)) { 'OFF' } else { 'ON' }
Log "CONFIGURE -> $bld  DESTDIR=$dest  CROSS_DRIVE=$crossDrive"
& cmake -S $src -B $bld -G "Visual Studio 17 2022" -A x64 -DDESTDIR="$dest" -DCMAKE_BUILD_TYPE=Release -DDEP_DEBUG=OFF -DORCA_INCLUDE_DEBUG_INFO=OFF -DDEPS_ARCH=x64 -DDEP_CROSS_DRIVE_BUILD=$crossDrive *>> $log
$cfg=$LASTEXITCODE; Log "configure exit=$cfg"
if($cfg -ne 0){ Set-Content $done "fail-configure-$cfg"; Log "########## DEPS ABORTED (configure) ##########"; exit $cfg }

# 4. Build deps
Log "BUILD target=deps (parallel msbuild)"
& cmake --build $bld --config Release --target deps -- -m *>> $log
$bex=$LASTEXITCODE; Log "build exit=$bex"
Set-Content $done "build-exit-$bex"
Log "########## DEPS DONE exit=$bex ##########"
exit $bex
