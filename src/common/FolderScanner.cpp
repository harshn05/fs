#include "FolderScanner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <utility>

namespace folder_scanner {
namespace {

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name) {
    std::wstring result = directory;

    if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
        result += L'\\';

    result += name;
    return result;
}

std::wstring MakeSearchPattern(const std::wstring& directory) {
    return JoinPath(directory, L"*");
}

/*
 * FindFirstFileExW with FindExInfoBasic avoids requesting short-name data.
 * LARGE_FETCH can improve enumeration on some filesystems. If the filesystem
 * or OS rejects that flag, retry without it.
 */
HANDLE FindDirectoryEntries(
    const std::wstring& directory,
    WIN32_FIND_DATAW& data) {
    const std::wstring pattern = MakeSearchPattern(directory);

    HANDLE handle = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED) {
            handle = FindFirstFileExW(
                pattern.c_str(),
                FindExInfoBasic,
                &data,
                FindExSearchNameMatch,
                nullptr,
                0);
        }
    }

    return handle;
}

class FindHandleGuard {
public:
    explicit FindHandleGuard(HANDLE handle) noexcept : handle_(handle) {}

    ~FindHandleGuard() {
        if (handle_ != INVALID_HANDLE_VALUE)
            FindClose(handle_);
    }

    FindHandleGuard(const FindHandleGuard&) = delete;
    FindHandleGuard& operator=(const FindHandleGuard&) = delete;

    HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_;
};

bool IsDotEntry(const wchar_t* name) {
    return name[0] == L'.' &&
        (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

bool IsReparsePoint(const WIN32_FIND_DATAW& data) {
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool IsDirectory(const WIN32_FIND_DATAW& data) {
    return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsNormalDirectory(const WIN32_FIND_DATAW& data) {
    return IsDirectory(data) && !IsReparsePoint(data);
}

bool IsEmptyDirectoryError(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES;
}

unsigned long long DirectorySizeNative(
    const std::wstring& root,
    const std::atomic<bool>& cancelRequested,
    unsigned long long& files,
    unsigned long long& errors) {
    unsigned long long total = 0;
    std::vector<std::wstring> pendingDirectories;
    pendingDirectories.push_back(root);

    while (!pendingDirectories.empty() && !cancelRequested.load()) {
        std::wstring current = std::move(pendingDirectories.back());
        pendingDirectories.pop_back();

        WIN32_FIND_DATAW data{};
        HANDLE findHandle = FindDirectoryEntries(current, data);

        if (findHandle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (!IsEmptyDirectoryError(error))
                ++errors;
            continue;
        }

        FindHandleGuard findGuard(findHandle);
        bool keepEnumerating = true;

        while (keepEnumerating && !cancelRequested.load()) {
            if (!IsDotEntry(data.cFileName) && !IsReparsePoint(data)) {
                if (IsDirectory(data)) {
                    pendingDirectories.push_back(
                        JoinPath(current, data.cFileName));
                } else {
                    const unsigned long long size =
                        (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
                        static_cast<unsigned long long>(data.nFileSizeLow);

                    total += size;
                    ++files;
                }
            }

            if (cancelRequested.load())
                break;

            if (!FindNextFileW(findGuard.get(), &data)) {
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_FILES && !cancelRequested.load())
                    ++errors;
                keepEnumerating = false;
            }
        }
    }

    return total;
}

} // namespace

std::wstring ToExtendedPath(const std::wstring& path) {
    if (path.rfind(LR"(\\?\)", 0) == 0)
        return path;

    if (path.rfind(LR"(\\)", 0) == 0)
        return LR"(\\?\UNC\)" + path.substr(2);

    if (path.size() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        return LR"(\\?\)" + path;
    }

    return path;
}

ScanResult ScanRoot(
    const std::wstring& root,
    const std::atomic<bool>& cancelRequested,
    const ProgressCallback& onFolderCompleted) {
    ScanResult result;
    const std::wstring extendedRoot = ToExtendedPath(root);

    WIN32_FIND_DATAW data{};
    HANDLE findHandle = FindDirectoryEntries(extendedRoot, data);

    if (findHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (!IsEmptyDirectoryError(error))
            ++result.rootErrors;
        result.cancelled = cancelRequested.load();
        return result;
    }

    FindHandleGuard findGuard(findHandle);
    bool keepEnumerating = true;

    while (keepEnumerating && !cancelRequested.load()) {
        if (!IsDotEntry(data.cFileName) && IsNormalDirectory(data)) {
            FolderInfo item;
            item.name = data.cFileName;
            item.path = JoinPath(extendedRoot, data.cFileName);
            item.bytes = DirectorySizeNative(
                item.path, cancelRequested, item.files, item.errors);

            result.items.push_back(std::move(item));

            if (onFolderCompleted) {
                onFolderCompleted(result.items.size(), result.items.back().name);
            }
        }

        if (cancelRequested.load())
            break;

        if (!FindNextFileW(findGuard.get(), &data)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES && !cancelRequested.load())
                ++result.rootErrors;
            keepEnumerating = false;
        }
    }

    result.cancelled = cancelRequested.load();
    return result;
}

} // namespace folder_scanner
