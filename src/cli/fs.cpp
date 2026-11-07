/*
 * ============================================================================
 *  Project      : Folder Size Explorer - CLI
 *  File         : fs.cpp
 *
 *  Copyright (c) 2026 Harsh Kumar Narula
 *
 *  Description  : A lightweight command-line companion to Folder Size Explorer.
 *                 By default, scans the immediate child directories of the
 *                 current working directory and reports their recursive sizes.
 *
 *  License      : MIT License
 *  Repository   : https://github.com/harshn05/FolderSizeTool
 * ============================================================================
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../common/FolderScanner.h"

namespace {

using FolderResult = folder_scanner::FolderInfo;
using ScanResult = folder_scanner::ScanResult;

std::atomic<bool> gCancelRequested{false};

// ---------------------------------------------------------------------------
// Console output
// ---------------------------------------------------------------------------

// Write UTF-16 directly to a real console. When stdout/stderr is redirected,
// emit UTF-8 instead so piping and redirection produce conventional text files.
bool WriteText(HANDLE output, std::wstring_view text) {
    if (output == nullptr || output == INVALID_HANDLE_VALUE || text.empty())
        return text.empty();

    DWORD consoleMode = 0;
    if (GetConsoleMode(output, &consoleMode)) {
        size_t offset = 0;
        while (offset < text.size()) {
            const DWORD chunk = static_cast<DWORD>(
                (text.size() - offset > 16384) ? 16384 : text.size() - offset);
            DWORD written = 0;
            if (!WriteConsoleW(output, text.data() + offset, chunk,
                               &written, nullptr) || written == 0) {
                return false;
            }
            offset += written;
        }
        return true;
    }

    // A Windows filename can contain unusual UTF-16 sequences. Use the
    // replacement behaviour of the UTF-8 converter rather than failing output.
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return false;

    std::string utf8(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            utf8.data(), required, nullptr, nullptr) != required) {
        return false;
    }

    size_t offset = 0;
    while (offset < utf8.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (utf8.size() - offset > 1024 * 1024) ? 1024 * 1024
                                                : utf8.size() - offset);
        DWORD written = 0;
        if (!WriteFile(output, utf8.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

void Print(std::wstring_view text) {
    WriteText(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void PrintError(std::wstring_view text) {
    WriteText(GetStdHandle(STD_ERROR_HANDLE), text);
}

std::wstring ErrorMessage(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring message;
    if (length && buffer) {
        message.assign(buffer, length);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ')) {
            message.pop_back();
        }
    }
    if (buffer)
        LocalFree(buffer);

    if (message.empty())
        message = L"Windows error " + std::to_wstring(error);
    return message;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::wstring FormatSize(unsigned long long bytes) {
    static constexpr const wchar_t* units[] = {
        L"B", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB"
    };

    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < (sizeof(units) / sizeof(units[0]))) {
        value /= 1024.0;
        ++unit;
    }

    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(unit == 0 ? 0 : 2)
           << value << L' ' << units[unit];
    return stream.str();
}

std::wstring Commas(unsigned long long value) {
    std::wstring result = std::to_wstring(value);
    for (int index = static_cast<int>(result.size()) - 3; index > 0;
         index -= 3) {
        result.insert(static_cast<size_t>(index), 1, L',');
    }
    return result;
}

std::wstring SafeDisplayName(std::wstring_view name) {
    std::wstring result(name);
    for (wchar_t& ch : result) {
        if (ch < 0x20 || ch == 0x7f)
            ch = L' ';
    }
    return result;
}

std::wstring FitName(std::wstring name, size_t width) {
    if (name.size() <= width)
        return name;

    if (width <= 3)
        return name.substr(0, width);

    name.resize(width - 3);
    // Do not leave a dangling UTF-16 high surrogate before the ellipsis.
    if (!name.empty() && name.back() >= 0xD800 && name.back() <= 0xDBFF)
        name.pop_back();
    name += L"...";
    return name;
}

// Resolve relative input paths before applying the extended-length prefix.
bool GetAbsolutePath(const std::wstring& input, std::wstring& absolutePath) {
    const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0)
        return false;

    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1);
    const DWORD length = GetFullPathNameW(
        input.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0 || static_cast<size_t>(length) >= buffer.size())
        return false;

    absolutePath.assign(buffer.data(), length);
    return true;
}

// Filesystem enumeration is provided by the shared scanner in src/common.

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

void PrintResults(const std::wstring& root, ScanResult& result) {
    std::sort(result.items.begin(), result.items.end(),
        [](const FolderResult& a, const FolderResult& b) {
            if (a.bytes != b.bytes)
                return a.bytes > b.bytes;
            const int comparison = CompareStringOrdinal(
                a.name.c_str(), -1, b.name.c_str(), -1, TRUE);
            return comparison == CSTR_LESS_THAN;
        });

    unsigned long long totalBytes = 0;
    unsigned long long totalFiles = 0;
    unsigned long long totalErrors = result.rootErrors;
    size_t nameWidth = 6;

    for (const auto& folder : result.items) {
        totalBytes += folder.bytes;
        totalFiles += folder.files;
        totalErrors += folder.errors;
        nameWidth = (std::max)(nameWidth,
            (std::min)(static_cast<size_t>(42),
                       SafeDisplayName(folder.name).size()));
    }

    const bool cancelled = result.cancelled;
    Print(L"\r\nRoot: " + SafeDisplayName(root) + L"\r\n");
    if (cancelled)
        Print(L"Status: CANCELLED (the displayed results may be partial)\r\n");
    else
        Print(L"Status: Scan completed\r\n");

    std::wostringstream table;
    table << L"\r\n" << std::left << std::setw(static_cast<int>(nameWidth))
          << L"Folder" << L"  "
          << std::right << std::setw(12) << L"Size" << L"  "
          << std::setw(16) << L"Bytes" << L"  "
          << std::setw(10) << L"Files" << L"  "
          << std::setw(8) << L"Errors" << L"\r\n";
    table << std::wstring(nameWidth + 54, L'-') << L"\r\n";

    for (const auto& folder : result.items) {
        const std::wstring name = FitName(SafeDisplayName(folder.name), nameWidth);
        table << std::left << std::setw(static_cast<int>(nameWidth)) << name
              << L"  " << std::right << std::setw(12) << FormatSize(folder.bytes)
              << L"  " << std::setw(16) << Commas(folder.bytes)
              << L"  " << std::setw(10) << Commas(folder.files)
              << L"  " << std::setw(8) << Commas(folder.errors) << L"\r\n";
    }

    table << std::wstring(nameWidth + 54, L'-') << L"\r\n";
    table << std::left << std::setw(static_cast<int>(nameWidth)) << L"TOTAL"
          << L"  " << std::right << std::setw(12) << FormatSize(totalBytes)
          << L"  " << std::setw(16) << Commas(totalBytes)
          << L"  " << std::setw(10) << Commas(totalFiles)
          << L"  " << std::setw(8) << Commas(totalErrors) << L"\r\n";
    Print(table.str());

    Print(L"\r\n" + std::to_wstring(result.items.size()) +
          L" folders | Total: " + FormatSize(totalBytes) +
          L" | " + Commas(totalFiles) + L" files | " +
          Commas(totalErrors) + L" errors\r\n");

    if (result.rootErrors) {
        Print(L"Root enumeration errors: " +
              Commas(result.rootErrors) + L"\r\n");
    }
}

BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT) {
        gCancelRequested = true;
        return TRUE;
    }
    return FALSE;
}

void PrintUsage(const wchar_t* executable) {
    Print(L"Folder Size Explorer CLI (fs)\r\n\r\n");
    Print(L"Usage:\r\n  " + std::wstring(executable) +
          L" [folder-path]\r\n\r\n");
    Print(L"If folder-path is omitted, the current working directory is scanned.\r\n");
    Print(L"Only immediate child folders are listed; their contents are scanned recursively.\r\n");
    Print(L"Press Ctrl+C to cancel a scan.\r\n");
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc > 2) {
        PrintError(L"Error: too many arguments.\r\n\r\n");
        PrintUsage(argv[0]);
        return 1;
    }

    if (argc == 2 &&
        (std::wstring_view(argv[1]) == L"-h" ||
         std::wstring_view(argv[1]) == L"--help" ||
         std::wstring_view(argv[1]) == L"/?")) {
        PrintUsage(argv[0]);
        return 0;
    }

    const std::wstring requestedPath = (argc == 2) ? argv[1] : L".";
    std::wstring absolutePath;
    if (!GetAbsolutePath(requestedPath, absolutePath)) {
        const DWORD error = GetLastError();
        PrintError(L"Error: could not resolve folder path: " +
                   SafeDisplayName(requestedPath) + L"\r\n" +
                   ErrorMessage(error) + L"\r\n");
        return 1;
    }

    const std::wstring extendedRoot = folder_scanner::ToExtendedPath(absolutePath);
    const DWORD attributes = GetFileAttributesW(extendedRoot.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        PrintError(L"Error: cannot access folder: " +
                   SafeDisplayName(absolutePath) + L"\r\n" +
                   ErrorMessage(error) + L"\r\n");
        return 1;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        PrintError(L"Error: the supplied path is not a directory.\r\n");
        return 1;
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    PrintError(L"Folder Size Explorer CLI (fs)\r\n");
    PrintError(L"Scanning root: " + SafeDisplayName(absolutePath) + L"\r\n\r\n");

    ScanResult result;
    try {
        result = folder_scanner::ScanRoot(
            absolutePath,
            gCancelRequested,
            [](std::size_t completedFolders, const std::wstring& folderName) {
                const std::wstring label =
                    (completedFolders == 1) ? L" folder: " : L" folders: ";
                PrintError(L"Scanned " + std::to_wstring(completedFolders) +
                           label + SafeDisplayName(folderName) + L"\r\n");
            });
    } catch (const std::exception& exception) {
        (void)exception;
        PrintError(L"Error: an unexpected C++ exception interrupted the scan.\r\n");
        SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
        return 1;
    } catch (...) {
        PrintError(L"Error: an unexpected failure interrupted the scan.\r\n");
        SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
        return 1;
    }

    PrintResults(absolutePath, result);
    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);

    if (result.cancelled)
        return 130;

    unsigned long long errors = result.rootErrors;
    for (const auto& folder : result.items)
        errors += folder.errors;
    return errors == 0 ? 0 : 2;
}
