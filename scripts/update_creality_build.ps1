# Record Creality's released build number for this checkout's tag in creality_release.txt.
#
#   update_creality_build.ps1            use the tag from `git describe --tags --abbrev=0`
#   update_creality_build.ps1 v7.3.2     use the given tag
#
# Our tags are Creality's tags. Creality's GitHub release for a tag names its Windows installer
# CrealityPrint_<a.b.c>.<build>_Release.exe; when a release has several, the highest build wins.
# version.inc reads creality_release.txt and stops the configure when it does not match the tag.
# Run it after each upstream sync. Ends with one line:  update_creality_build: <a.b.c.build>
param([string]$Tag)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Repo = 'CrealityOfficial/CrealityPrint'

function Fail($msg) { Write-Output "update_creality_build: refused: $msg"; exit 2 }

$git = (Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $git) { Fail 'git.exe not on PATH - install Git for Windows' }
$gh = (Get-Command gh.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $gh) { Fail 'gh.exe not on PATH - install the GitHub CLI (winget install GitHub.cli), then run gh auth login' }

if (-not $Tag) {
  $Tag = (& $git -C $RepoRoot describe --tags --abbrev=0 2>$null)
  if (-not $Tag) { Fail "no tag found in $RepoRoot - pass one: update_creality_build.ps1 v7.3.2" }
}
if ($Tag -notmatch '^[vV]?(\d+\.\d+\.\d+)$') { Fail "tag '$Tag' is not vA.B.C - pass Creality's tag, e.g. v7.3.2" }
$xyz = $Matches[1]

$names = & $gh api "repos/$Repo/releases/tags/$Tag" --jq '.assets[].name'
if ($LASTEXITCODE -ne 0) { Fail "no GitHub release $Tag in $Repo (gh exit $LASTEXITCODE) - check the tag, or run gh auth login" }

$pattern = '^CrealityPrint_' + [regex]::Escape($xyz) + '\.(\d+)_Release\.exe$'
$builds = @($names | ForEach-Object { if ($_ -match $pattern) { [int]$Matches[1] } })
if (-not $builds) { Fail "release $Tag has no CrealityPrint_$xyz.<build>_Release.exe asset - assets: $($names -join ', ')" }

$version = "$xyz.$(($builds | Measure-Object -Maximum).Maximum)"
Set-Content -Encoding ascii -NoNewline -Path (Join-Path $RepoRoot 'creality_release.txt') -Value $version
Write-Output "update_creality_build: $version"
exit 0
