@echo off
rem ===========================================================================
rem  build_mingw.bat - builds minimap.asi with MinGW-w64 (32-bit / i686)
rem
rem  Usage:
rem    build_mingw.bat                     <- g++ from PATH or a known folder
rem    build_mingw.bat C:\mingw32\bin      <- explicit toolchain bin folder
rem
rem  The toolchain MUST be the i686 (32-bit) build. A x86_64 MinGW cannot
rem  produce -m32 code, and this plugin is 32-bit only (every address in
rem  src/game.h is a 32-bit VA of gta_sa.exe 1.0 US).
rem ===========================================================================
setlocal

cd /d "%~dp0"

set "GXX="

rem --- 1. explicit argument -------------------------------------------------
if not "%~1"=="" (
    if exist "%~1\g++.exe" set "GXX=%~1\g++.exe"
    if exist "%~1"       if "%~x1"==".exe" set "GXX=%~1"
)

rem --- 2. PATH --------------------------------------------------------------
if not defined GXX (
    where g++ >nul 2>nul && set "GXX=g++"
)

rem --- 3. common extraction folders -----------------------------------------
if not defined GXX (
    for %%D in (
        "C:\mingw32\bin"
        "C:\mingw\bin"
        "C:\mingw64\bin"
        "C:\msys64\mingw32\bin"
        "%LOCALAPPDATA%\mingw32\bin"
        "%USERPROFILE%\mingw32\bin"
        "%~dp0toolchain\bin"
        "%~dp0mingw32\bin"
    ) do (
        if not defined GXX if exist "%%~D\g++.exe" set "GXX=%%~D\g++.exe"
    )
)

if not defined GXX (
    echo [build] g++ not found.
    echo [build] Download winlibs i686 MinGW-w64, extract it, then either:
    echo [build]   build_mingw.bat C:\mingw32\bin
    echo [build] or add C:\mingw32\bin to PATH and run build_mingw.bat again.
    exit /b 1
)

rem --- 4. make sure it really is a 32-bit target ----------------------------
for /f "usebackq delims=" %%T in (`"%GXX%" -dumpmachine`) do set "TRIPLET=%%T"
echo [build] compiler : %GXX%
echo [build] target   : %TRIPLET%
echo %TRIPLET% | findstr /i "i686 i386 mingw32" >nul
if errorlevel 1 (
    echo [build] WARNING: this is not an i686 toolchain.
    echo [build]          A x86_64 MinGW cannot build this 32-bit plugin.
    echo [build]          Continuing anyway - it will probably fail to link.
)

if not exist build mkdir build

echo [build] compiling...
"%GXX%" -m32 -std=c++17 -O2 -s ^
    -shared -static -static-libgcc -static-libstdc++ ^
    -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -Isrc ^
    src\main.cpp src\radar.cpp src\config.cpp src\hook.cpp src\thunk.cpp src\log.cpp ^
    -o build\minimap.asi ^
    -luser32 -lgdi32 ^
    -Wl,--kill-at -Wl,--enable-stdcall-fixup

if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)

echo [build] done: build\minimap.asi
echo [build] copy build\minimap.asi + minimap_config.ini into the GTA:SA folder
endlocal
