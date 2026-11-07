@echo off
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if %errorlevel%==0 (
    echo Building with MSVC...
    cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE FolderSizeTool.cpp /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib ole32.lib comdlg32.lib user32.lib /OUT:FolderSizeTool.exe
    if errorlevel 1 exit /b 1
) else (
    where g++ >nul 2>nul
    if errorlevel 1 (
        echo ERROR: cl.exe or g++.exe was not found.
        echo Run this from a Visual Studio Developer Command Prompt,
        echo or load your WinDev MSVC environment first.
        pause
        exit /b 1
    )
    echo Building with MinGW...
    g++ -std=c++17 -O2 -municode -mwindows ^
    -static -static-libgcc -static-libstdc++ ^
    FolderSizeTool.cpp -o FolderSizeTool.exe ^
    -lcomctl32 -lshell32 -lole32 -lcomdlg32 -luser32 -luuid
    if errorlevel 1 exit /b 1
)
echo.
echo Build successful: %CD%\FolderSizeTool.exe
pause
