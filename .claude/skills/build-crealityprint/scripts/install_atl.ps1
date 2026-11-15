$ErrorActionPreference='Continue'
# Build root: $env:CPBUILD_ROOT, else D:\cpbuild when a D: drive exists, else C:\cpbuild.
$CpbuildRoot = if ($env:CPBUILD_ROOT) { $env:CPBUILD_ROOT } elseif (Test-Path 'D:\') { 'D:\cpbuild' } else { 'C:\cpbuild' }
New-Item -ItemType Directory -Force $CpbuildRoot | Out-Null
$log="$CpbuildRoot\atl_install.log"; $done="$CpbuildRoot\atl.done"
Remove-Item $done -ErrorAction SilentlyContinue
function L($m){ Add-Content $log "$((Get-Date).ToString('HH:mm:ss')) $m" }
L "===== ATL install v4 (no --wait; poll for header) ====="
$setup = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe'
$ip = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
# NOTE: 'setup.exe modify' does NOT support --wait (causes exit 87). Use --quiet --norestart and poll.
$argline = "modify --installPath `"$ip`" --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.VC.ATLMFC --quiet --norestart"
L "launch: setup.exe $argline"
$p = Start-Process -FilePath $setup -ArgumentList $argline -Wait -PassThru
L "setup.exe returned exitcode=$($p.ExitCode)"
# The actual install may continue in a background service; poll until the ATL header appears.
$msvc = (Get-ChildItem "$ip\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$atlHeader = Join-Path $msvc 'atlmfc\include\atlbase.h'
for($i=0; $i -lt 240; $i++){
  if(Test-Path $atlHeader){ break }
  Start-Sleep -Seconds 5
}
$present = Test-Path $atlHeader
L "atlbase.h present=$present ($atlHeader)"
Set-Content $done "setupexit-$($p.ExitCode)-atl-$present"
