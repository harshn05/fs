# Folder Size Explorer

[![Windows CI](https://github.com/harshn05/FolderSizeTool/actions/workflows/windows-ci.yml/badge.svg?branch=main)](https://github.com/harshn05/FolderSizeTool/actions/workflows/windows-ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A lightweight, native Windows utility that shows how much space each immediate subfolder consumes. Select a root directory, scan its child folders recursively, sort the results, and export a CSV report.

## Features

- **Recursive folder sizing:** Calculates the total logical file size of each immediate child directory.
- **Fast native enumeration:** Uses `FindFirstFileExW` and `FindNextFileW`, reading file sizes directly from directory enumeration metadata.
- **Sortable results:** Sort by folder name, size, byte count, file count, or scan errors.
- **Open folders:** Double-click a result or select it and choose **Open selected**.
- **CSV export:** Exports folder names and scan statistics as UTF-8 CSV with a BOM for spreadsheet compatibility.
- **Cancellation:** Stop an active scan and start another scan without restarting the application.
- **Long-path scanning:** Uses extended-length Windows paths for filesystem enumeration.
- **No third-party C++ dependencies:** Built directly on the Win32 API and the C++ standard library.

## Build

The project supports 64-bit MSVC and MinGW-w64.

### Option 1: Build.bat

Run `Build.bat` from either:

- **MSVC:** an x64 Visual Studio Developer Command Prompt with the C++ build tools and Windows SDK available.
- **MinGW-w64:** an x64 MinGW-w64 environment with `g++` available. The supplied command is configured for MSYS2 UCRT64.

The script detects `cl.exe` first and otherwise uses `g++`. The executable is written beside the source as `FolderSizeTool.exe`.

### Option 2: Build manually

**MSVC (x64):**

```bat
cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE FolderSizeTool.cpp /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib ole32.lib comdlg32.lib user32.lib /OUT:FolderSizeTool.exe
```

**MinGW-w64 (MSYS2 UCRT64):**

```bash
g++ -std=c++17 -O2 -municode -mwindows \
  -static -static-libgcc -static-libstdc++ \
  FolderSizeTool.cpp -o FolderSizeTool.exe \
  -lcomctl32 -lshell32 -lole32 -lcomdlg32 -luser32 -luuid
```

The MinGW command statically links the GCC and C++ runtime libraries. Windows system libraries remain operating-system dependencies.

## Usage

1. Launch `FolderSizeTool.exe`.
2. Select **Choose root folder...** and choose a directory.
3. Select **Scan**.
4. Click a column header to sort the results.
5. Double-click a folder to open it, or use **Open selected**.
6. Select **Export CSV** to save the report.

The **Size** and **Bytes** columns show the total logical size of files found under each immediate child folder. **Files** is the number of files counted. **Errors** reports enumeration or metadata errors encountered while scanning that child folder.

## Scan behaviour and limitations

- The scan is read-only. It does not create, modify, or delete the scanned files.
- The tool reports **logical file sizes**, not allocated disk space on storage devices. Sparse files and filesystem compression can therefore make allocated space different from the reported size.
- The selected root's own files are not listed separately; the report contains its immediate child directories.
- Reparse-point entries are skipped. This avoids following directory symlinks and junctions, but also means file reparse points are not counted.
- Inaccessible directories and files are skipped where possible. Check the **Errors** column for scan errors that were detected; some access-denied entries may be skipped by Windows enumeration.
- Extended-length paths are used for scanning. Opening an extremely long path through the Windows shell may still be limited by shell/application path handling.
- Scan duration depends on the number of entries, storage device, filesystem, and access permissions.

## Continuous integration

GitHub Actions builds the project independently with:

- MSVC x64
- MinGW-w64 UCRT64

Each successful workflow run uploads the corresponding executable as a downloadable artifact. These CI jobs verify compilation; interactive scanning behaviour should also be tested on Windows.

See [GitHub Actions](https://github.com/harshn05/FolderSizeTool/actions).

## License

This project is licensed under the [MIT License](LICENSE).

---

Repository: [github.com/harshn05/FolderSizeTool](https://github.com/harshn05/FolderSizeTool)
