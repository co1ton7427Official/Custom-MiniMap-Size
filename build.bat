@echo off
rem ===========================================================================
rem  build.bat - builds minimap.asi with Visual Studio (x86)
rem
rem  Run from a "x86 Native Tools Command Prompt for VS" or let this script
rem  locate vcvars32.bat itself. Output: build\minimap.asi
rem ===========================================================================
setlocal

cd /d "%~dp0"

rem --- locate the compiler -------------------------------------------------
where cl >nul 2>nul
if errorlevel 1 goto findvc
goto havecl

:findvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] cl.exe not found and vswhere.exe missing.
    echo [build] Open an "x86 Native Tools Command Prompt for VS" and run build.bat again.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [build] No Visual Studio C++ installation found.
    exit /b 1
)

if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" (
    echo [build] vcvars32.bat not found under "%VSPATH%".
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 (
    echo [build] vcvars32.bat failed.
    exit /b 1
)

:havecl
if not exist build mkdir build

echo [build] compiling...
cl /nologo /LD /O2 /MT /EHsc /W4 /permissive- /Gy /GS- ^
   /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   /Isrc ^
   src\main.cpp src\radar.cpp src\config.cpp src\hook.cpp src\thunk.cpp src\log.cpp ^
   /Fo:build\ /Fe:build\minimap.asi ^
   /link /SUBSYSTEM:WINDOWS /DLL /DYNAMICBASE:NO /NXCOMPAT:NO user32.lib

if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)

del /q build\*.obj >nul 2>nul

echo [build] done: build\minimap.asi
echo [build] copy build\minimap.asi and minimap_config.ini into the GTA:SA folder
endlocal
