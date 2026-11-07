@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where cl.exe >nul 2>nul
if not errorlevel 1 goto build_msvc

where g++.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cl.exe or g++.exe was not found.
    echo Run this from an x64 Visual Studio Developer Command Prompt,
    echo or load your WinDev MSVC / MinGW environment first.
    exit /b 1
)
goto build_mingw

:build_msvc
echo Building fs-gui.exe with MSVC x64...
cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE src\gui\FolderSizeTool.cpp src\common\FolderScanner.cpp /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib ole32.lib comdlg32.lib user32.lib /OUT:fs-gui.exe
if errorlevel 1 exit /b 1
goto success

:build_mingw
echo Building fs-gui.exe with MinGW-w64...
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ src\gui\FolderSizeTool.cpp src\common\FolderScanner.cpp -o fs-gui.exe -lcomctl32 -lshell32 -lole32 -lcomdlg32 -luser32 -luuid
if errorlevel 1 exit /b 1
goto success

:success
echo.
echo Build successful: %CD%\fs-gui.exe
exit /b 0
