/*
 * ============================================================================
 *  Project      : Folder Size Explorer
 *  File         : FolderSizeTool.cpp
 *
 *  Copyright (c) 2026 Harsh Kumar Narula
 *  Author       : Harsh Kumar Narula
 *  Email        : harsh.narula@iitbombay.org
 *
 *  Description  : A lightweight native Windows utility for analyzing
 *                 recursive folder sizes.
 *
 *  License      : MIT License
 *  Repository   : https://github.com/harshn05/fs
 * ============================================================================
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/FolderScanner.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")

using Item = folder_scanner::FolderInfo;
using ScanResult = folder_scanner::ScanResult;

static HWND gMainWindow = nullptr;
static HWND gList = nullptr;
static HWND gStatus = nullptr;
static HWND gChoose = nullptr;
static HWND gScan = nullptr;
static HWND gCancel = nullptr;
static HWND gExport = nullptr;
static HWND gOpen = nullptr;

static std::wstring gRoot;
static std::vector<Item> gItems;
static std::thread gWorker;

static std::atomic<bool> gBusy{false};
static std::atomic<bool> gCancelRequested{false};
static std::atomic<bool> gShuttingDown{false};

// Worker-to-UI result handoff. No heap pointer is passed in a window message.
static std::mutex gResultMutex;
static ScanResult gPendingResult;
static bool gHasPendingResult = false;

// Signalled by the worker after publishing its completed ScanResult.
static HANDLE gScanCompletedEvent = nullptr;

static constexpr UINT WM_SCAN_PROGRESS = WM_APP + 2;

// ============================================================================
// Formatting helpers
// ============================================================================

static std::wstring FormatSize(unsigned long long n) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};

    double value = static_cast<double>(n);
    int unit = 0;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::wostringstream stream;
    stream << std::fixed << std::setprecision(unit == 0 ? 0 : 2)
           << value << L" " << units[unit];
    return stream.str();
}

static std::wstring Commas(unsigned long long n) {
    std::wstring value = std::to_wstring(n);

    for (int i = static_cast<int>(value.size()) - 3; i > 0; i -= 3)
        value.insert(static_cast<size_t>(i), 1, L',');

    return value;
}

static void SetStatus(const std::wstring& value) {
    if (gStatus)
        SetWindowTextW(gStatus, value.c_str());
}

// Folder enumeration is implemented by the shared scanner in src/common.

// ============================================================================
// ListView helpers
// ============================================================================

static void AddRow(const Item& item) {
    const int row = ListView_GetItemCount(gList);

    LVITEMW listItem{};
    listItem.mask = LVIF_TEXT;
    listItem.iItem = row;
    listItem.pszText = const_cast<wchar_t*>(item.name.c_str());
    ListView_InsertItem(gList, &listItem);

    std::wstring size = FormatSize(item.bytes);
    std::wstring raw = Commas(item.bytes);
    std::wstring files = Commas(item.files);
    std::wstring errors = item.errors ? Commas(item.errors) : L"";

    ListView_SetItemText(gList, row, 1, size.data());
    ListView_SetItemText(gList, row, 2, raw.data());
    ListView_SetItemText(gList, row, 3, files.data());
    ListView_SetItemText(gList, row, 4, errors.data());
}

static void RefreshRows() {
    ListView_DeleteAllItems(gList);

    for (const auto& item : gItems)
        AddRow(item);
}

static void SortItems(int column) {
    static int sortColumn = 1;
    static bool sortAscending = false;

    if (sortColumn == column)
        sortAscending = !sortAscending;
    else {
        sortColumn = column;
        sortAscending = (column == 0);
    }

    std::sort(gItems.begin(), gItems.end(),
        [&](const Item& a, const Item& b) {
            int comparison = 0;

            if (column == 0) {
                comparison = _wcsicmp(a.name.c_str(), b.name.c_str());
            }
            else if (column == 1 || column == 2) {
                comparison = a.bytes < b.bytes ? -1 : a.bytes > b.bytes ? 1 : 0;
            }
            else if (column == 3) {
                comparison = a.files < b.files ? -1 : a.files > b.files ? 1 : 0;
            }
            else {
                comparison = a.errors < b.errors ? -1 : a.errors > b.errors ? 1 : 0;
            }

            return sortAscending ? comparison < 0 : comparison > 0;
        });

    RefreshRows();
}

// ============================================================================
// Folder picker
// ============================================================================

static std::wstring PickFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)
    );

    if (FAILED(hr) || !dialog)
        return {};

    DWORD options = 0;
    hr = dialog->GetOptions(&options);

    if (SUCCEEDED(hr)) {
        hr = dialog->SetOptions(
            options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }

    if (SUCCEEDED(hr))
        hr = dialog->Show(owner);

    std::wstring selectedPath;

    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);

        if (SUCCEEDED(hr) && item) {
            PWSTR path = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);

            if (SUCCEEDED(hr) && path) {
                selectedPath = path;
                CoTaskMemFree(path);
            }

            item->Release();
        }
    }

    dialog->Release();
    return selectedPath;
}

// ============================================================================
// Scanning controls and worker
// ============================================================================

static void SetScanningControls(bool scanning) {
    EnableWindow(gChoose, !scanning);
    EnableWindow(gScan, !scanning);
    EnableWindow(gCancel, scanning);
    EnableWindow(gExport, !scanning && !gItems.empty());
}

static void StartScan() {
    if (gBusy.load() || gRoot.empty() || gShuttingDown.load())
        return;

    if (gWorker.joinable())
        gWorker.join();

    {
        std::lock_guard<std::mutex> lock(gResultMutex);
        gPendingResult = {};
        gHasPendingResult = false;
    }

    ResetEvent(gScanCompletedEvent);

    gBusy = true;
    gCancelRequested = false;
    gItems.clear();

    ListView_DeleteAllItems(gList);
    SetScanningControls(true);
    SetStatus(L"Scanning folders...");

    const std::wstring root = gRoot;

    try {
        gWorker = std::thread([root]() {
            ScanResult completed;

            try {
                completed = folder_scanner::ScanRoot(
                    root,
                    gCancelRequested,
                    [](size_t completedFolders, const std::wstring&) {
                        // Progress is best-effort. Completion is signalled by an event.
                        if (!gShuttingDown.load()) {
                            PostMessageW(
                                gMainWindow,
                                WM_SCAN_PROGRESS,
                                static_cast<WPARAM>(completedFolders),
                                0
                            );
                        }
                    });
            }
            catch (...) {
                // Preserve already collected results if an allocation or
                // unexpected C++ exception occurs during the scan.
                ++completed.rootErrors;
            }

            {
                std::lock_guard<std::mutex> lock(gResultMutex);
                gPendingResult = std::move(completed);
                gHasPendingResult = true;
            }

            // Event notification cannot lose ownership of the result.
            SetEvent(gScanCompletedEvent);
        });
    }
    catch (const std::exception&) {
        gBusy = false;
        SetScanningControls(false);
        SetStatus(L"Could not start the scan worker thread.");
        MessageBoxW(
            gMainWindow,
            L"Could not start the scan worker thread.",
            L"Scan failed",
            MB_ICONERROR
        );
    }
}

// ============================================================================
// Scan completion
// ============================================================================

static void CompleteScan() {
    if (gShuttingDown.load())
        return;

    ScanResult completed;
    bool hasResult = false;

    {
        std::lock_guard<std::mutex> lock(gResultMutex);

        if (gHasPendingResult) {
            completed = std::move(gPendingResult);
            gPendingResult = {};
            gHasPendingResult = false;
            hasResult = true;
        }
    }

    if (!hasResult)
        return;

    if (gWorker.joinable())
        gWorker.join();

    gItems = std::move(completed.items);
    gBusy = false;

    std::sort(gItems.begin(), gItems.end(),
        [](const Item& a, const Item& b) {
            return a.bytes > b.bytes;
        });

    RefreshRows();
    SetScanningControls(false);

    unsigned long long total = 0;
    for (const auto& item : gItems)
        total += item.bytes;

    std::wstring status =
        (gCancelRequested.load() ? L"Cancelled. " : L"Done. ") +
        std::to_wstring(gItems.size()) + L" folders | Total: " +
        FormatSize(total);

    if (completed.rootErrors) {
        status += L" | Root scan errors: " +
            std::to_wstring(completed.rootErrors);
    }

    status += L" | Double-click a row to open it.";
    SetStatus(status);
}

// ============================================================================
// CSV export
// ============================================================================

static std::wstring CsvEscape(const std::wstring& value) {
    std::wstring escaped = L"\"";

    for (wchar_t ch : value) {
        if (ch == L'"')
            escaped += L"\"\"";
        else
            escaped += ch;
    }

    escaped += L'"';
    return escaped;
}

static bool WriteAll(HANDLE file, const char* data, size_t length) {
    while (length > 0) {
        const DWORD chunk = static_cast<DWORD>(
            (length > 1024 * 1024) ? 1024 * 1024 : length);
        DWORD written = 0;

        if (!WriteFile(file, data, chunk, &written, nullptr) || written == 0)
            return false;

        data += written;
        length -= written;
    }

    return true;
}

static void ExportCsv(HWND owner) {
    if (gItems.empty())
        return;

    wchar_t file[MAX_PATH] = L"folder-sizes.csv";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"csv";

    if (!GetSaveFileNameW(&dialog))
        return;

    std::wstring wideData =
        L"Folder,Size (bytes),Size (human),Files,Errors\r\n";

    for (const auto& item : gItems) {
        wideData += CsvEscape(item.name) + L"," +
            std::to_wstring(item.bytes) + L"," +
            CsvEscape(FormatSize(item.bytes)) + L"," +
            std::to_wstring(item.files) + L"," +
            std::to_wstring(item.errors) + L"\r\n";
    }

    const int requiredBytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wideData.data(),
        static_cast<int>(wideData.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (requiredBytes <= 0) {
        MessageBoxW(owner, L"Could not convert the report to UTF-8.",
            L"Export failed", MB_ICONERROR);
        return;
    }

    std::string utf8(static_cast<size_t>(requiredBytes), '\0');

    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wideData.data(),
            static_cast<int>(wideData.size()),
            utf8.data(),
            requiredBytes,
            nullptr,
            nullptr) != requiredBytes) {
        MessageBoxW(owner, L"Could not convert the report to UTF-8.",
            L"Export failed", MB_ICONERROR);
        return;
    }

    HANDLE output = CreateFileW(
        file,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (output == INVALID_HANDLE_VALUE) {
        MessageBoxW(owner, L"Could not create the CSV file.",
            L"Export failed", MB_ICONERROR);
        return;
    }

    static const char bom[] = "\xEF\xBB\xBF";
    const bool success =
        WriteAll(output, bom, sizeof(bom) - 1) &&
        WriteAll(output, utf8.data(), utf8.size());

    const bool closeSuccess = CloseHandle(output) != FALSE;

    if (!success || !closeSuccess) {
        MessageBoxW(
            owner,
            L"An error occurred while writing the CSV file. "
            L"Check available disk space and permissions.",
            L"Export failed",
            MB_ICONERROR
        );
        return;
    }

    SetStatus(L"Exported: " + std::wstring(file));
}

// ============================================================================
// Open selected folder
// ============================================================================

static void OpenSelected() {
    const int row = ListView_GetNextItem(gList, -1, LVNI_SELECTED);

    if (row < 0 || row >= static_cast<int>(gItems.size()))
        return;

    std::wstring path = gItems[static_cast<size_t>(row)].path;

    if (path.rfind(LR"(\\?\UNC\)", 0) == 0)
        path = LR"(\\)" + path.substr(8);
    else if (path.rfind(LR"(\\?\)", 0) == 0)
        path = path.substr(4);

    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
}

// ============================================================================
// Shutdown
// ============================================================================

static void CancelScanAndJoin() {
    if (!gWorker.joinable())
        return;

    gCancelRequested = true;
    gWorker.join();
    gBusy = false;
}

// ============================================================================
// Window procedure
// ============================================================================

static LRESULT CALLBACK WndProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        gChoose = CreateWindowW(
            L"BUTTON", L"Choose root folder...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 12, 160, 30, window, (HMENU)101, nullptr, nullptr);

        gScan = CreateWindowW(
            L"BUTTON", L"Scan",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            180, 12, 80, 30, window, (HMENU)102, nullptr, nullptr);

        gCancel = CreateWindowW(
            L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            268, 12, 80, 30, window, (HMENU)105, nullptr, nullptr);
        EnableWindow(gCancel, FALSE);

        gExport = CreateWindowW(
            L"BUTTON", L"Export CSV",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            356, 12, 100, 30, window, (HMENU)103, nullptr, nullptr);

        gOpen = CreateWindowW(
            L"BUTTON", L"Open selected",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            464, 12, 115, 30, window, (HMENU)104, nullptr, nullptr);

        gStatus = CreateWindowW(
            L"STATIC", L"Choose a root folder, then click Scan.",
            WS_CHILD | WS_VISIBLE,
            12, 50, 850, 22, window, nullptr, nullptr, nullptr);

        gList = CreateWindowW(
            WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS,
            12, 78, 850, 480, window, (HMENU)106,
            GetModuleHandleW(nullptr), nullptr);

        ListView_SetExtendedListViewStyle(
            gList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        const wchar_t* headers[] = {
            L"Folder", L"Size", L"Bytes", L"Files", L"Errors"
        };
        const int widths[] = {360, 120, 150, 100, 90};

        for (int i = 0; i < 5; ++i) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(headers[i]);
            column.cx = widths[i];
            column.iSubItem = i;
            ListView_InsertColumn(gList, i, &column);
        }

        return 0;
    }

    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);

        MoveWindow(gStatus, 12, 50, width - 24, 22, TRUE);
        MoveWindow(gList, 12, 78, width - 24, height - 90, TRUE);

        const int widths[] = {width - 520, 120, 150, 100, 90};
        for (int i = 0; i < 5; ++i)
            ListView_SetColumnWidth(gList, i, widths[i]);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 101: {
            const std::wstring selected = PickFolder(window);

            if (!selected.empty()) {
                gRoot = selected;
                gItems.clear();
                ListView_DeleteAllItems(gList);
                SetStatus(L"Root: " + gRoot);
                EnableWindow(gExport, FALSE);
            }

            return 0;
        }
        case 102:
            StartScan();
            return 0;
        case 103:
            ExportCsv(window);
            return 0;
        case 104:
            OpenSelected();
            return 0;
        case 105:
            if (gBusy.load()) {
                SetStatus(L"Cancelling scan...");
                gCancelRequested = true;
                EnableWindow(gCancel, FALSE);
            }
            return 0;
        }
        break;

    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<NMHDR*>(lParam);

        if (notification->hwndFrom == gList &&
            notification->code == LVN_COLUMNCLICK) {
            const auto* column = reinterpret_cast<NMLISTVIEW*>(lParam);
            SortItems(column->iSubItem);
        }

        if (notification->hwndFrom == gList &&
            notification->code == NM_DBLCLK) {
            OpenSelected();
        }

        break;
    }

    case WM_SCAN_PROGRESS:
        // Ignore queued progress updates after scan completion or shutdown.
        if (!gShuttingDown.load() && gBusy.load()) {
            SetStatus(L"Scanning... " + std::to_wstring(wParam) +
                L" folders completed.");
        }
        return 0;

    case WM_CLOSE:
        gShuttingDown = true;

        if (gBusy.load()) {
            SetStatus(L"Stopping scan before exit...");
            CancelScanAndJoin();
        }

        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        gShuttingDown = true;
        CancelScanAndJoin();

        {
            std::lock_guard<std::mutex> lock(gResultMutex);
            gPendingResult = {};
            gHasPendingResult = false;
        }

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

// ============================================================================
// Application entry point
// ============================================================================

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    const HRESULT comResult = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED);

    gScanCompletedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!gScanCompletedEvent) {
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    INITCOMMONCONTROLSEX controls{
        sizeof(controls), ICC_LISTVIEW_CLASSES
    };
    InitCommonControlsEx(&controls);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"FolderSizeToolWindow";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&windowClass)) {
        CloseHandle(gScanCompletedEvent);
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    gMainWindow = CreateWindowW(
        windowClass.lpszClassName,
        L"Folder Size Explorer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        nullptr, nullptr, instance, nullptr);

    if (!gMainWindow) {
        CloseHandle(gScanCompletedEvent);
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    ShowWindow(gMainWindow, showCommand);
    UpdateWindow(gMainWindow);

    // Wait for scan completion or ordinary Windows messages.
    int exitCode = 0;
    bool running = true;

    while (running) {
        const DWORD waitResult = MsgWaitForMultipleObjects(
            1,
            &gScanCompletedEvent,
            FALSE,
            INFINITE,
            QS_ALLINPUT
        );

        if (waitResult == WAIT_OBJECT_0) {
            ResetEvent(gScanCompletedEvent);
            CompleteScan();
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) {
            MSG message{};

            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    exitCode = static_cast<int>(message.wParam);
                    running = false;
                    break;
                }

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        else {
            exitCode = 1;
            running = false;
        }
    }

    // Defensive cleanup before releasing the completion event.
    gShuttingDown = true;
    CancelScanAndJoin();

    {
        std::lock_guard<std::mutex> lock(gResultMutex);
        gPendingResult = {};
        gHasPendingResult = false;
    }

    CloseHandle(gScanCompletedEvent);
    gScanCompletedEvent = nullptr;

    if (SUCCEEDED(comResult))
        CoUninitialize();

    return exitCode;
}
