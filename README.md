# Folder Size Explorer

A small native Windows utility to find which immediate subfolders consume the most disk space.

## Features
- Choose any root folder using a folder picker.
- Recursively calculates each immediate subfolder's total file size.
- Sort by folder name, size, bytes, file count, or scan errors by clicking a column.
- Double-click a row or use **Open selected** to open that folder.
- Export results to UTF-8 CSV.
- Skips inaccessible entries where possible and does not follow directory symlinks.

## Build
Run `Build.bat` from:
- a Visual Studio Developer Command Prompt, or
- a MinGW-w64 environment with a Windows compiler.

It produces `FolderSizeTool.exe` beside the source.

No third-party libraries or runtime installation are required for the native build.

## Notes
The scan is read-only. It does not delete or modify files. Folder sizes are logical file sizes, not allocated disk-cluster sizes. The scan may take time for very large trees.
