
/*
 * ============================================================================
 *  Project      : Folder Size Explorer
 *  File         : FolderSizeTool.cpp
 *
 *  Copyright (c) 2026 Harsh Kumar Narula
 *  Author       : Harsh Kumar Narula
 *
 *  Description  : A lightweight native Windows utility for analyzing
 *                 recursive folder sizes.
 *
 *  License      : MIT License
 *
 *  Repository   : https://github.com/harshn05/FolderSizeTool
 *
 * ============================================================================
 */


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cwctype>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

struct Item {
    std::wstring name, path;
    unsigned long long bytes = 0;
    unsigned long long files = 0;
    unsigned long long errors = 0;
};

static HWND gMainWindow, gList, gStatus, gChoose, gScan, gExport, gOpen;
static std::wstring gRoot;
static std::vector<Item> gItems;
static std::atomic<bool> gBusy{false};
static int gSortColumn = 1;
static bool gSortAscending = false;
static constexpr UINT WM_SCAN_DONE = WM_APP + 1;
static constexpr UINT WM_SCAN_PROGRESS = WM_APP + 2;

std::wstring FormatSize(unsigned long long n) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::wostringstream s;
    s << std::fixed << std::setprecision(u == 0 ? 0 : 2) << v << L" " << units[u];
    return s.str();
}
std::wstring Commas(unsigned long long n) {
    std::wstring s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, 1, L',');
    return s;
}
void SetStatus(const std::wstring& s) { SetWindowTextW(gStatus, s.c_str()); }

unsigned long long DirectorySize(const fs::path& p, unsigned long long& files, unsigned long long& errors) {
    unsigned long long total = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) ++errors;
    for (; it != end; it.increment(ec)) {
        if (ec) { ++errors; ec.clear(); continue; }
        std::error_code e2;
        if (it->is_symlink(e2)) { if (it->is_directory(e2)) it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(e2)) {
            auto n = it->file_size(e2);
            if (!e2) { total += n; ++files; } else ++errors;
        }
    }
    return total;
}

void AddRow(const Item& x) {
    int i = ListView_GetItemCount(gList);
    LVITEMW li{}; li.mask = LVIF_TEXT; li.iItem = i;
    li.pszText = const_cast<wchar_t*>(x.name.c_str());
    ListView_InsertItem(gList, &li);
    std::wstring size = FormatSize(x.bytes), raw = Commas(x.bytes), files = Commas(x.files);
    ListView_SetItemText(gList, i, 1, size.data());
    ListView_SetItemText(gList, i, 2, raw.data());
    ListView_SetItemText(gList, i, 3, files.data());
    std::wstring err = x.errors ? Commas(x.errors) : L"";
    ListView_SetItemText(gList, i, 4, err.data());
}
void RefreshRows() {
    ListView_DeleteAllItems(gList);
    for (const auto& x : gItems) AddRow(x);
}
void SortItems(int col) {
    if (gSortColumn == col) gSortAscending = !gSortAscending;
    else { gSortColumn = col; gSortAscending = col == 0; }
    std::sort(gItems.begin(), gItems.end(), [&](const Item& a, const Item& b) {
        int c = 0;
        if (col == 0) c = _wcsicmp(a.name.c_str(), b.name.c_str());
        else if (col == 1 || col == 2) c = a.bytes < b.bytes ? -1 : a.bytes > b.bytes ? 1 : 0;
        else if (col == 3) c = a.files < b.files ? -1 : a.files > b.files ? 1 : 0;
        else c = a.errors < b.errors ? -1 : a.errors > b.errors ? 1 : 0;
        return gSortAscending ? c < 0 : c > 0;
    });
    RefreshRows();
}
void StartScan() {
    if (gBusy || gRoot.empty()) return;
    gBusy = true;
    EnableWindow(gChoose, FALSE); EnableWindow(gScan, FALSE); EnableWindow(gExport, FALSE);
    ListView_DeleteAllItems(gList);
    SetStatus(L"Scanning folders... Large directories may take a while.");
    std::wstring root = gRoot;
    std::thread([root]() {
        std::vector<Item> result;
        std::error_code ec;
        fs::directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            std::error_code e;
            if (!it->is_directory(e) || it->is_symlink(e)) continue;
            Item x;
            x.name = it->path().filename().wstring();
            x.path = it->path().wstring();
            x.bytes = DirectorySize(it->path(), x.files, x.errors);
            result.push_back(std::move(x));
            PostMessageW(gMainWindow, WM_SCAN_PROGRESS, static_cast<WPARAM>(result.size()), 0);
        }
        auto* out = new std::vector<Item>(std::move(result));
        PostMessageW(gMainWindow, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(out));
    }).detach();
}
std::wstring PickFolder(HWND owner) {
    BROWSEINFOW bi{}; bi.hwndOwner = owner; bi.lpszTitle = L"Choose a root folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (!pid) return {};
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(pid, path) != FALSE;
    CoTaskMemFree(pid);
    return ok ? path : L"";
}
void ExportCsv(HWND owner) {
    if (gItems.empty()) return;
    wchar_t file[MAX_PATH] = L"folder-sizes.csv";
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"csv";
    if (!GetSaveFileNameW(&ofn)) return;
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { MessageBoxW(owner, L"Could not create the CSV file.", L"Export failed", MB_ICONERROR); return; }
    std::wstring data = L"Folder,Size (bytes),Size (human),Files,Errors\r\n";
    for (const auto& x : gItems) {
        std::wstring name = L"\"" + x.name + L"\"";
        size_t pos = 0; while ((pos = name.find(L"\"", pos + 1)) != std::wstring::npos) { name.insert(pos, 1, L'"'); ++pos; }
        data += name + L"," + std::to_wstring(x.bytes) + L",\"" + FormatSize(x.bytes) + L"\"," + std::to_wstring(x.files) + L"," + std::to_wstring(x.errors) + L"\r\n";
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0) { WideCharToMultiByte(CP_UTF8, 0, data.c_str(), -1, utf8.data(), bytes, nullptr, nullptr); utf8.pop_back(); }
    DWORD written = 0; WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr); CloseHandle(h);
    SetStatus(L"Exported: " + std::wstring(file));
}
void OpenSelected() {
    int i = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
    if (i < 0 || i >= static_cast<int>(gItems.size())) return;
    ShellExecuteW(nullptr, L"open", gItems[i].path.c_str(), nullptr, nullptr, SW_SHOW);
}
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        gChoose = CreateWindowW(L"BUTTON", L"Choose root folder...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 12, 12, 160, 30, h, (HMENU)101, nullptr, nullptr);
        gScan = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 180, 12, 80, 30, h, (HMENU)102, nullptr, nullptr);
        gExport = CreateWindowW(L"BUTTON", L"Export CSV", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 268, 12, 100, 30, h, (HMENU)103, nullptr, nullptr);
        gOpen = CreateWindowW(L"BUTTON", L"Open selected", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 376, 12, 115, 30, h, (HMENU)104, nullptr, nullptr);
        gStatus = CreateWindowW(L"STATIC", L"Choose a root folder, then click Scan.", WS_CHILD|WS_VISIBLE, 12, 50, 850, 22, h, nullptr, nullptr, nullptr);
        gList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS, 12, 78, 850, 480, h, (HMENU)105, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        const wchar_t* heads[] = {L"Folder", L"Size", L"Bytes", L"Files", L"Errors"};
        int widths[] = {360, 120, 150, 100, 90};
        for (int i=0;i<5;++i) { LVCOLUMNW c{}; c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM; c.pszText=const_cast<wchar_t*>(heads[i]); c.cx=widths[i]; c.iSubItem=i; ListView_InsertColumn(gList,i,&c); }
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(l), height = HIWORD(l);
        MoveWindow(gStatus, 12, 50, width-24, 22, TRUE);
        MoveWindow(gList, 12, 78, width-24, height-90, TRUE);
        for (int i=0;i<5;++i) { int wds[]={width-520,120,150,100,90}; ListView_SetColumnWidth(gList,i,wds[i]); }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 101: { auto p=PickFolder(h); if(!p.empty()){gRoot=p; SetStatus(L"Root: "+gRoot); gItems.clear(); ListView_DeleteAllItems(gList);} return 0; }
        case 102: StartScan(); return 0;
        case 103: ExportCsv(h); return 0;
        case 104: OpenSelected(); return 0;
        }
        break;
    case WM_NOTIFY: {
        auto* nm=reinterpret_cast<NMHDR*>(l);
        if(nm->hwndFrom==gList && nm->code==LVN_COLUMNCLICK){ auto* n=reinterpret_cast<NMLISTVIEW*>(l); SortItems(n->iSubItem); }
        if(nm->hwndFrom==gList && nm->code==NM_DBLCLK) OpenSelected();
        break;
    }
    case WM_SCAN_PROGRESS: SetStatus(L"Scanning... " + std::to_wstring(w) + L" folders completed."); return 0;
    case WM_SCAN_DONE: {
        auto* p=reinterpret_cast<std::vector<Item>*>(l);
        if(p){gItems=std::move(*p);delete p;}
        std::sort(gItems.begin(),gItems.end(),[](const Item&a,const Item&b){return a.bytes>b.bytes;});
        RefreshRows(); gBusy=false;
        EnableWindow(gChoose,TRUE); EnableWindow(gScan,TRUE); EnableWindow(gExport,TRUE);
        unsigned long long total=0; for(auto&x:gItems) total+=x.bytes;
        SetStatus(L"Done. " + std::to_wstring(gItems.size()) + L" folders | Total: " + FormatSize(total) + L" | Double-click a row to open it.");
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=inst; wc.lpszClassName=L"FolderSizeToolWindow";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"Folder Size Explorer",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,900,620,nullptr,nullptr,inst,nullptr);
    gMainWindow=h; ShowWindow(h,show); UpdateWindow(h);
    MSG msg; while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return static_cast<int>(msg.wParam);
}
