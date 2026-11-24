@echo off
rem ===========================================================================
rem  build_zig.bat - build minimap.asi with the bundled Zig toolchain
rem  (zig ships a complete clang + lld + MinGW-w64 sysroot, so no install and
rem   no MSVC are needed; it compiles for 32-bit Windows just fine).
rem ===========================================================================
setlocal
cd /d "%~dp0"

set "ZIG=%~dp0_tc\py\ziglang\zig.exe"
if not exist "%ZIG%" (
    echo [build] zig not found at %ZIG%
    exit /b 1
)

set "TEMP=%~dp0_tc\tmp"
set "TMP=%~dp0_tc\tmp"
set "ZIG_GLOBAL_CACHE_DIR=%~dp0_tc\zigcache"
set "ZIG_LOCAL_CACHE_DIR=%~dp0_tc\ziglocal"
if not exist "%~dp0_tc\tmp" mkdir "%~dp0_tc\tmp"
if not exist "%ZIG_GLOBAL_CACHE_DIR%" mkdir "%ZIG_GLOBAL_CACHE_DIR%"
if not exist "%ZIG_LOCAL_CACHE_DIR%" mkdir "%ZIG_LOCAL_CACHE_DIR%"
if not exist "build" mkdir "build"

"%ZIG%" c++ -target x86-windows-gnu -std=c++17 -O2 -s ^
    -fno-exceptions -fno-rtti -fno-threadsafe-statics ^
    -ffunction-sections -fdata-sections ^
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I"%~dp0src" ^
    -shared -static -static-libgcc -static-libstdc++ ^
    -Wl,--gc-sections ^
    "%~dp0src\main.cpp" "%~dp0src\radar.cpp" "%~dp0src\config.cpp" ^
    "%~dp0src\hook.cpp" "%~dp0src\thunk.cpp" "%~dp0src\log.cpp" ^
    -o "%~dp0build\minimap.asi" ^
    -luser32

if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)
echo [build] ok: build\minimap.asi
endlocal
