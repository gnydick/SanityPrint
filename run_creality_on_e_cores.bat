@echo off
chcp 65001 >nul
title SanityPrint - Efficiency Core Launcher
cd /d "%~dp0"
set "BATDIR=%~dp0"

echo =================================
echo    SanityPrint Efficiency Core Launcher
echo =================================
echo.

if not exist "SanityPrint.exe" (
    echo [Error] Could not find SanityPrint.exe in %~dp0
    pause
    exit /b 1
)

echo Starting application...
echo Pinning to the second half of CPU cores...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$app = Join-Path -Path $env:BATDIR -ChildPath 'SanityPrint.exe';" ^
    "if(-not (Test-Path $app)){ Write-Error 'SanityPrint.exe not found.'; exit 1 }" ^
    "$p = Start-Process $app -PassThru;" ^
    "if($p){" ^
    "  Start-Sleep -Milliseconds 1500;" ^
    "  $c = 8; try{ $c = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors } catch {};" ^
    "  $ptrBits = [IntPtr]::Size * 8;" ^
    "  $usable = [math]::Min($c, $ptrBits);" ^
    "  $half = [math]::Max(1, [math]::Floor($usable / 2));" ^
    "  $mask64 = [UInt64]0;" ^
    "  for($i = $usable - $half; $i -lt $usable; $i++){ $mask64 = $mask64 -bor ([UInt64]1 -shl $i) }" ^
    "  if($mask64 -eq 0){ Write-Error 'Could not compute affinity mask.'; exit 1 }" ^
    "  if([IntPtr]::Size -eq 4){ $ptr = [Int32]($mask64 -band 0xFFFFFFFF) } else { $ptr = [Int64]$mask64 }" ^
    "  $p.ProcessorAffinity = [IntPtr]$ptr;" ^
    "  Write-Host \"Program started (using CPU $($usable-$half)-$($usable-1))\" }"

echo.
echo Done. This window will close in 3 seconds...
timeout /t 3 >nul
exit /b 0
