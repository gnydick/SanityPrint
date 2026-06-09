$ErrorActionPreference = 'Continue'
$log  = 'D:\cpbuild\app_build.log'
$done = 'D:\cpbuild\app.done'
Remove-Item $done -ErrorAction SilentlyContinue
function Log($m){ $ts=(Get-Date).ToString('HH:mm:ss'); Add-Content -Path $log -Value "$ts  $m" }

Log "########## APP BUILD START ##########"

# 1. Refresh PATH from registry
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
foreach($d in @('C:\Strawberry\perl\bin','C:\Strawberry\c\bin','C:\Program Files\NASM')){
  if((Test-Path $d) -and ($env:Path -notlike "*$d*")){ $env:Path = "$d;" + $env:Path }
}
# Pin CMake 3.31.x (avoid CMake 4.x removals against this legacy tree)
$cmakeBin = 'D:\cpbuild\tools\cmake-3.31.12-windows-x86_64\bin'
$env:Path = "$cmakeBin;" + $env:Path
$cmake = Join-Path $cmakeBin 'cmake.exe'

# 2. Import VS developer environment
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if(-not (Test-Path $vswhere)){ $vswhere = 'I:\IdeaProjects\SanityPrint\tools\vswhere.exe' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" && set" | ForEach-Object { if($_ -match '^(.*?)=(.*)$'){ [Environment]::SetEnvironmentVariable($matches[1],$matches[2]) } }
Log ("cmake = " + (& $cmake --version | Select-Object -First 1))
Log ("cl    = " + (Get-Command cl -ErrorAction SilentlyContinue).Source)

# Official release build number (the public squashed repo does not carry it). version.inc
# appends this to the git-tag-derived version, yielding 7.1.1.4472 to match the release.
$env:BUILD_ID = '4472'
Log "BUILD_ID=$env:BUILD_ID"

# 3. Sanity: deps must be present
$deps = 'D:\cpbuild\OrcaSlicer_dep\usr\local'
if(-not (Test-Path $deps)){ Log "FATAL: deps prefix missing: $deps"; Set-Content $done 'fail-no-deps'; exit 1 }

# 4. Configure app
$src='I:\IdeaProjects\SanityPrint'; $bld='D:\cpbuild\app'
Log "CONFIGURE -> $bld  PREFIX=$deps"
& $cmake -S $src -B $bld -G "Visual Studio 17 2022" -A x64 `
    -U SANITYPRINT_VERSION `
    -DBBL_RELEASE_TO_PUBLIC=1 `
    -DPROJECT_VERSION_EXTRA=Release `
    -DCMAKE_PREFIX_PATH="$deps" `
    -DCMAKE_INSTALL_PREFIX="$bld\OrcaSlicer" `
    -DCMAKE_BUILD_TYPE=Release `
    -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0" *>> $log
$cfg=$LASTEXITCODE; Log "configure exit=$cfg"
if($cfg -ne 0){ Set-Content $done "fail-configure-$cfg"; Log "########## APP ABORTED (configure) ##########"; exit $cfg }

# 5. Build the WHOLE Windows build (ALL_BUILD = every target in the solution).
Log "BUILD target=ALL_BUILD (parallel)"
& $cmake --build $bld --config Release --target ALL_BUILD -- -m *>> $log
$bex=$LASTEXITCODE; Log "build exit=$bex"
Set-Content $done "build-exit-$bex"
Log "########## APP DONE exit=$bex ##########"
exit $bex
