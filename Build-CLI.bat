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
echo Building fs.exe with MSVC x64...
cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE src\cli\fs.cpp src\common\FolderScanner.cpp /link /SUBSYSTEM:CONSOLE /OUT:fs.exe
if errorlevel 1 exit /b 1
goto success

:build_mingw
echo Building fs.exe with MinGW-w64...
g++ -std=c++17 -O2 -municode -mconsole -static -static-libgcc -static-libstdc++ src\cli\fs.cpp src\common\FolderScanner.cpp -o fs.exe
if errorlevel 1 exit /b 1
goto success

:success
echo.
echo Build successful: %CD%\fs.exe
echo Usage: fs.exe [folder-path]
exit /b 0
