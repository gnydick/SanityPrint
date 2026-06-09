@REM Unified OrcaSlicer build script for Windows with single build directory
@echo off
set WP=%CD%

@REM Check for help
if "%1"=="help" (
    echo Usage: build_vs2022_unified.bat [target] [options]
    echo.
    echo Targets:
    echo   deps       - Build dependencies ^(Release + Debug^)
    echo   deps-debug - Build Debug dependencies only ^(assumes Release deps exist^)
    echo   slicer     - Build slicer only ^(assumes deps are built^)
    echo   all        - Build both deps and slicer ^(default^)
    echo   clean      - Clean build directory
    echo   open       - Open Visual Studio solution
    echo.
    echo Build type is controlled in Visual Studio ^(Debug/Release/RelWithDebInfo^)
    echo.
    exit /b 0
)

set build_dir=build_unified
set target=%1
if "%target%"=="" set target=all

@REM Clean
if "%target%"=="clean" (
    echo Cleaning build directory...
    if exist %build_dir% rmdir /s /q %build_dir%
    if exist deps\%build_dir% rmdir /s /q deps\%build_dir%
    echo Done.
    exit /b 0
)

setlocal DISABLEDELAYEDEXPANSION 

@REM Build dependencies
if "%target%"=="deps" GOTO :deps
if "%target%"=="deps-debug" GOTO :deps_debug
if "%target%"=="all" GOTO :deps
if "%target%"=="slicer" GOTO :slicer
if "%target%"=="open" GOTO :open

echo Invalid target: %target%
echo Run with "help" for usage
exit /b 1

:deps
echo ========================================
echo Building dependencies...
echo ========================================
cd deps
mkdir %build_dir% 2>nul
cd %build_dir%
set DEPS=%CD%/OrcaSlicer_dep

echo Configuring dependencies with CMake...
echo cmake ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%CD%/OrcaSlicer_dep" -DDEP_DEBUG=ON -DORCA_INCLUDE_DEBUG_INFO=OFF
cmake ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%CD%/OrcaSlicer_dep" -DDEP_DEBUG=ON -DORCA_INCLUDE_DEBUG_INFO=OFF
if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Building dependencies for Release configuration...
echo This may take 30-60 minutes depending on your system...
cmake --build . --config Release --target deps -- -m
if %ERRORLEVEL% NEQ 0 (
    echo Dependencies Release build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Building dependencies for Debug configuration...
echo This may take another 30-60 minutes...
cmake --build . --config Debug --target deps -- -m
if %ERRORLEVEL% NEQ 0 (
    echo Dependencies Debug build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo Dependencies built successfully!
echo ========================================
echo Location: deps\%build_dir%
echo Both Release and Debug configurations are built.
echo.

if "%target%"=="deps" exit /b 0
cd %WP%
GOTO :slicer

:deps_debug
echo ========================================
echo Building Debug dependencies only...
echo ========================================
cd deps
cd %build_dir%
if not exist "OrcaSlicer_dep\usr\local" (
    echo ERROR: Release dependencies not found!
    echo Please build all dependencies first: build_vs2022_unified.bat deps
    exit /b 1
)

echo Building dependencies for Debug configuration...
cmake --build . --config Debug --target deps -- -m
if %ERRORLEVEL% NEQ 0 (
    echo Dependencies Debug build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo Debug dependencies built successfully!
echo ========================================
exit /b 0

:slicer
echo ========================================
echo Building Orca Slicer...
echo ========================================
cd %WP%

@REM Ensure deps are built
if not exist "deps\%build_dir%\OrcaSlicer_dep\usr\local" (
    echo ERROR: Dependencies not found!
    echo Please build dependencies first: build_vs2022_unified.bat deps
    exit /b 1
)

set DEPS=%WP%\deps\%build_dir%\OrcaSlicer_dep

mkdir %build_dir% 2>nul
cd %build_dir%

echo Configuring Orca Slicer with CMake...
echo cmake .. -G "Visual Studio 17 2022" -A x64 -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0"
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DBBL_RELEASE_TO_PUBLIC=1 ^
    -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" ^
    -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" ^
    -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0"

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Building Orca Slicer for Release configuration...
cmake --build . --config Release --target ALL_BUILD -- -m
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Running gettext localization processing...
cd %WP%
call run_gettext.bat

echo.
echo Installing Release build...
cd %build_dir%
cmake --build . --target install --config Release

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Build directory: %build_dir%
echo Solution file: %build_dir%\SanityPrint.sln
echo.
echo To build Debug configuration:
echo   1. Open Visual Studio: build_vs2022_unified.bat open
echo   2. Select Debug configuration in toolbar
echo   3. Build Solution ^(Ctrl+Shift+B^)
echo.
echo Or from command line:
echo   cmake --build %build_dir% --config Debug --target ALL_BUILD -- -m
echo   cmake --build %build_dir% --config Debug --target install
echo.

exit /b 0

:open
if not exist "%build_dir%\SanityPrint.sln" (
    echo ERROR: Solution file not found!
    echo Please build the project first: build_vs2022_unified.bat all
    exit /b 1
)

echo Opening Visual Studio...
start "" "%build_dir%\SanityPrint.sln"
exit /b 0
