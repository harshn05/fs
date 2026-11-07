#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace folder_scanner {

struct FolderInfo {
    std::wstring name;
    std::wstring path;
    unsigned long long bytes = 0;
    unsigned long long files = 0;
    unsigned long long errors = 0;
};

struct ScanResult {
    std::vector<FolderInfo> items;
    unsigned long long rootErrors = 0;
    bool cancelled = false;
};

// Called after each immediate child folder has been measured.
using ProgressCallback = std::function<void(
    std::size_t completedFolders, const std::wstring& completedFolderName)>;

// Converts drive-letter and UNC paths to extended-length Windows paths.
std::wstring ToExtendedPath(const std::wstring& path);

// Scans the immediate child directories of root and measures their contents
// recursively. The caller owns the cancellation flag and may set it from
// another thread (for example, from a GUI button or console Ctrl+C handler).
ScanResult ScanRoot(
    const std::wstring& root,
    const std::atomic<bool>& cancelRequested,
    const ProgressCallback& onFolderCompleted = {});

} // namespace folder_scanner
