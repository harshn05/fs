# Folder Size Explorer

[![Windows CI](https://github.com/harshn05/fs/actions/workflows/windows-ci.yml/badge.svg?branch=main)](https://github.com/harshn05/fs/actions/workflows/windows-ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A lightweight native Windows folder-size utility with two frontends: a graphical application and a command-line tool. Both use the same C++17 Win32 directory-scanning engine.

## Applications

### Folder Size Explorer (GUI)

`fs-gui.exe` lets you select a root directory, recursively calculate the logical size of each immediate child folder, sort the results, open a selected folder, and export a UTF-8 CSV report.

Features:

- Native Win32 directory enumeration using `FindFirstFileExW` and `FindNextFileW`.
- Folder size, exact byte count, file count, and detected enumeration errors.
- Sortable result columns.
- Open selected folders or double-click a result.
- UTF-8 CSV export with a BOM for spreadsheet compatibility.
- Cancel an active scan and start another scan without restarting the application.
- Unicode names and extended-length path scanning.

### Folder Size Explorer CLI (`fs.exe`)

`fs.exe` prints the immediate child folders of the current working directory, sorted from largest to smallest, with their recursive size, exact bytes, file count, and errors.

Run it from the directory you want to inspect:

```bat
fs
```

Or provide a folder path:

```bat
fs "D:\Development"
```

Show usage:

```bat
fs --help
```

Place `fs.exe` in a directory on `PATH` to invoke it from any working directory. Otherwise, run it by its full path.

The CLI writes the report to standard output and scan progress/errors to standard error. Redirect the report to a UTF-8 text file like this:

```bat
fs "D:\Data" > folder-sizes.txt
```

Press **Ctrl+C** to cancel a scan. Cancelled output is marked as partial.

Exit codes:

- `0`: scan completed without detected errors.
- `1`: invalid arguments, inaccessible root, or startup failure.
- `2`: scan completed with one or more detected enumeration errors.
- `130`: scan cancelled with Ctrl+C.

## Build

The project supports 64-bit MSVC and MinGW-w64 (MSYS2 UCRT64).

### Build the GUI

```bat
Build-GUI.bat
```

Output: `fs-gui.exe`

### Build the CLI

```bat
Build-CLI.bat
```

Output: `fs.exe`

Both build scripts detect `cl.exe` first and otherwise use `g++`. Run them from an x64 Visual Studio Developer Command Prompt or an environment with the MinGW-w64 compiler on `PATH`.

## Project structure

```text
fs/
├── src/
│   ├── common/
│   │   ├── FolderScanner.h
│   │   └── FolderScanner.cpp
│   ├── gui/
│   │   └── FolderSizeTool.cpp
│   └── cli/
│       └── fs.cpp
├── Build-GUI.bat
├── Build-CLI.bat
├── LICENSE
└── .github/workflows/windows-ci.yml
```

The GUI and CLI share `FolderScanner`. Filesystem traversal, file-size accounting, reparse-point handling, long-path conversion, and scan-error counting are implemented once in the common library.

## Scan behaviour and limitations

- The scanner reports **logical file sizes**, not allocated disk space. Sparse files and filesystem compression can therefore make allocated space different from the reported size.
- The selected root's own files are not listed separately. Only its immediate child directories are returned, with their contents measured recursively.
- Reparse-point entries are skipped. This prevents traversing directory symlinks and junctions, and also means file reparse points are not counted.
- Inaccessible entries are skipped where possible and detected enumeration failures are counted. Some inaccessible entries may be skipped by Windows enumeration without a separately reported entry-level error.
- Extended-length paths are used for filesystem enumeration. Opening extremely long paths through the Windows shell may still be limited by shell/application path handling.
- Scan duration depends on the number of entries, filesystem, storage device, and access permissions.
- The tool is read-only. It does not create, modify, or delete scanned files.

## Continuous integration

GitHub Actions builds both frontends independently with:

- MSVC x64
- MinGW-w64 UCRT64

The workflow uploads four temporary build artifacts: GUI and CLI for each compiler. Artifacts are retained for 7 days. CI verifies compilation; interactive filesystem behaviour should also be tested on Windows.

See [GitHub Actions](https://github.com/harshn05/fs/actions).

## License

MIT License. Copyright (c) 2026 Harsh Kumar Narula.
