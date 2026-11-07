# Applying the shared-scanner refactor

This package is an overlay for the existing `harshn05/FolderSizeTool` repository.

1. Extract the archive into the repository root, allowing `README.md`, `Build.bat`, and `.github/workflows/windows-ci.yml` to be replaced.
2. The old root-level `FolderSizeTool.cpp` has moved to `src/gui/FolderSizeTool.cpp`. Remove the old root-level copy after confirming the new file is present:

   ```bat
   git rm FolderSizeTool.cpp
   ```

3. Review the resulting changes:

   ```bat
   git status --short
   ```

4. Build the GUI and CLI separately:

   ```bat
   Build-GUI.bat
   Build-CLI.bat
   ```

5. Test both executables against the same known test folders before committing.

`Build.bat` remains a backward-compatible alias for the GUI build.

The shared scanner progress callback reports both the completed-folder count and the name of the folder just completed. The GUI currently uses the count for its status message; the CLI prints both values.
