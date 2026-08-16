// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.1
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         *
// @compilerOptions -lGdi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Fixes white flashes when opening new windows.

### Before
![Before](https://raw.githubusercontent.com/MGGSK/MGGSK/refs/heads/main/WindhawkModReadmeImages/fix-explorer-white-flash-before.png)

### After
![After](https://raw.githubusercontent.com/MGGSK/MGGSK/refs/heads/main/WindhawkModReadmeImages/fix-explorer-white-flash-after.png)

*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- GlobalSettings:
    - backgroundColor: "0x191919"
      $name: Background Color
      $description: Enter HEX (#RRGGBB or 0xRRGGBB) or RGB (25,25,25)
      $options:
        - "0x191919": Dark Gray
        - "0x1E1E1E": Charcoal
        - "0x2D2D2D": Slate
        - "0x3A3A3A": Steel
        - "0x000000": Black
        - "0xFFFFFF": White
        
    - aggressivePaint: true
      $name: Aggressive Painting
      $description: >-
        Aggressively search for white regions and paint them in with the chosen color.
        May affect the appearance and rendering of windows! 
        Fixes white flickering while changing the window size.

    - longerPaint: true
      $name: Longer Painting
      $description: >-
        Paint the white regions for a longer period. 
        Window elements will appear more slowly. 
        Guaranteed to paint all windows and child elements.

  $name: Global Settings
  $description: These settings affect all processes and their windows.
  
- ProcessesSettings:
  - - name: ""
      $name: Process Name
      $description: base name + .exe (explorer.exe, notepad++.exe)
    
    - backgroundColor: "0x191919"
      $name: Background Color
      $description: Enter HEX (#RRGGBB or 0xRRGGBB) or RGB (25,25,25)
      $options:
        - "0x191919": Dark Gray
        - "0x1E1E1E": Charcoal
        - "0x2D2D2D": Slate
        - "0x3A3A3A": Steel
        - "0x000000": Black
        - "0xFFFFFF": White
        
    - aggressivePaint: true
      $name: Aggressive Painting
      $description: >-
        Aggressively search for white regions and fill them in with the chosen color.
        May affect the appearance and rendering of windows! 
        Fixes white flickering while changing the window size.

    - longerPaint: true
      $name: Longer Painting
      $description: >-
        Paint the white regions for a longer period. 
        Window elements will appear more slowly. 
        Guaranteed to paint all windows and child elements.

  $name: Settings per Process
  $description: >-
    You can set individual parameters for each process. 
    Click "Add new item" below to add a new process.
*/
// ==/WindhawkModSettings==


#include <windhawk_utils.h>
#include <mutex>
#include <unordered_map>
#include <string>

#define DCX_USESTYLE  0x00010000L
#define DEFAULT_COLOR 0x191919

using std::wstring;
using lock_t = std::lock_guard<std::mutex>;
using DefProcCallback = WNDPROC;


struct ProcessData {
    DWORD processId = 0;
    DWORD threadId  = 0;
    wstring name;
};

struct ProcessSettings {
    HBRUSH brush;
    bool aggressivePaint;
    bool longerPaint;
};


struct {
    HBRUSH brush;
    bool aggressivePaint;
    bool longerPaint;
    std::unordered_map<wstring, ProcessSettings> processes;
} g_cfg;


std::mutex g_cfgMutex;
std::mutex g_filledMutex;
std::unordered_map<HWND, bool> g_filledWindows;         // prevents painting after full rendering

std::mutex g_ownersMutex;
std::unordered_map<HWND, ProcessData> g_windowsOwners;  // holds cache for windows


decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;


// Helpers
bool ShouldSkip(HWND hWnd, bool aggressivePaint) {
    lock_t lock(g_filledMutex);
    if (g_filledWindows.contains(hWnd))
        return true;

    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    if (g_filledWindows.contains(hRoot))
        return true;

    if (aggressivePaint)
        return false;

    // Contains frame
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION) || !(style & WS_THICKFRAME))
        return true; 

    // Top-level window
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_LAYERED))
        return true;

    return false;
}

void MarkToSkip(HWND hWnd) {
    // Mark this window as "rendered": 
    // painting is no longer required
    if (g_filledWindows.contains(hWnd))
        return;

    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    
    lock_t lock(g_filledMutex);
    g_filledWindows[hRoot] = true;
    g_filledWindows[hWnd]  = true;    
}

void RemoveMarkSkip(HWND hWnd) {
    {
        lock_t lock(g_filledMutex);
        auto it = g_filledWindows.find(hWnd);
        if (it != g_filledWindows.end()) {
            g_filledWindows.erase(it);
        }
    }
    {
        HWND hRoot = GetAncestor(hWnd, GA_ROOT);
        lock_t lock(g_filledMutex);
        auto it = g_filledWindows.find(hRoot);
        if (it != g_filledWindows.end()) {
            g_filledWindows.erase(it);
        }
    }
}

int Clamp(int value, int low, int high) {
    if (value < low) 
        return low;
    if (value > high) 
        return high;

    return value;
}

COLORREF ToColor(int rgb) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8)  & 0xFF;
    int b = rgb & 0xFF;

    return RGB(r, g, b);
}

wstring GetProcessName(HWND hWnd) {
    DWORD ownerPid = 0;
    const DWORD ownerTid = GetWindowThreadProcessId(hWnd, &ownerPid);
    {
        lock_t lock(g_ownersMutex);
        auto it = g_windowsOwners.find(hWnd);

        if (it != g_windowsOwners.end()
         && it->second.processId == ownerPid
         && it->second.threadId  == ownerTid) {
            return it->second.name;
        }

        if (it != g_windowsOwners.end()) 
            g_windowsOwners.erase(it);
    }

    if (!ownerPid) {
        return L"";
    }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ownerPid);
    if (!hProc) {
        return L"";
    }

    wstring procName = L"";
    WCHAR exePath[MAX_PATH] = {0};
    DWORD exePathLen = MAX_PATH;

    if (QueryFullProcessImageNameW(hProc, 0, exePath, &exePathLen)) {
        WCHAR* name = wcsrchr(exePath, L'\\');
        if (name) {
            procName = (name + 1);
            size_t dotPos = procName.find(L'.');
            if (dotPos != wstring::npos) {
                procName = procName.substr(0, dotPos);
            }
            std::transform(
                procName.begin(), 
                procName.end(), 
                procName.begin(), 
                ::towlower
            );
        }
    }

    CloseHandle(hProc);

    if (!procName.empty()) {
        lock_t lock(g_ownersMutex);
        g_windowsOwners[hWnd] = ProcessData { ownerPid, ownerTid, procName };
        // return pointer stored in the map to ensure stable lifetime
        return g_windowsOwners[hWnd].name;
    }

    return L"";
}

ProcessSettings GetProcessSettings(HWND hWnd) {
    wstring name = GetProcessName(hWnd);
    if (!name.empty()) {
        auto it = g_cfg.processes.find(name);
        if (it != g_cfg.processes.end())
            return it->second;
    }

    return {
        g_cfg.brush,
        g_cfg.aggressivePaint, 
        g_cfg.longerPaint
    };
}


LRESULT FillWindow(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, DefProcCallback restore) {
    // Covers the white background with a colored rectangle while the window is rendering.
    switch (Msg) {
    case WM_NCPAINT: {
        // This message usually appears first 
        // and it's common for windows.
        // We're painting the non-client area first and 
        // then restore() draws chrome elements 
        // (caption buttons, borders) on top of it.
        auto cfg = GetProcessSettings(hWnd);
        if (ShouldSkip(hWnd, cfg.aggressivePaint))
             break;
            
        HRGN hrgn = (HRGN)wParam;
        HDC  hdc;
        // paint the NC area
        if (hrgn == (HRGN)1) {
            hdc = GetDCEx(hWnd, NULL, DCX_WINDOW | DCX_USESTYLE | 0);
        } else {
            hdc = GetDCEx(hWnd, hrgn, DCX_WINDOW | DCX_USESTYLE | DCX_INTERSECTRGN);
        }

        if (!hdc) 
            break;

        RECT rect;
        if (!GetWindowRect(hWnd, &rect)) {
            ReleaseDC(hWnd, hdc);
            break;
        }

        rect = { 
            0, 0,  // left upper corner
            rect.right  - rect.left, 
            rect.bottom - rect.top          
        };

        FillRect(hdc, &rect, cfg.brush);
        ReleaseDC(hWnd, hdc);
        
        if (!cfg.aggressivePaint)
            MarkToSkip(hWnd);

        break;
    }
    case WM_ERASEBKGND: {
        // This message is common for dialogs.
        // Apply the rectangle fill and cover the
        // white background during window rendering.
        // It will be removed later so that the 
        // window elements become visible.
        auto cfg = GetProcessSettings(hWnd);
        if (ShouldSkip(hWnd, cfg.aggressivePaint))
            break;

        RECT rect;
        if (!GetClientRect(hWnd, &rect))
            break;

        HDC hdc = (HDC)wParam;
        if (!hdc) {
            break;
        }

        FillRect(hdc, &rect, cfg.brush);
        ReleaseDC(hWnd, hdc);
        MarkToSkip(hWnd);

        return TRUE; // background erased - don't let the original erase it again
    } 
    case WM_SETCURSOR:
    case WM_ENTERSIZEMOVE: {
        // Window is rendered, don't paint again
        MarkToSkip(hWnd);
        
        break;
    }
    case WM_NCDESTROY: {
        // Window is destroyed, allow painting later
        RemoveMarkSkip(hWnd);
        break;
    }
    } // switch

    return restore(hWnd, Msg, wParam, lParam);
}


template <typename... Args>
int ParseColorSetting(PCWSTR valueName, Args... args) {
    PCWSTR value = Wh_GetStringSetting(valueName, args...);
    if (!value) {
        Wh_FreeStringSetting(value);
        return DEFAULT_COLOR;
    }

    const wchar_t* p = value;
    while (*p && iswspace(*p)) 
        ++p;

    if (!*p) {
        Wh_FreeStringSetting(value);
        return DEFAULT_COLOR;
    }

    if (wcsncmp(p, L"rgb", 3) == 0) {
        const wchar_t* open  = wcschr(p, L'(');
        const wchar_t* close = wcschr(p, L')');
        if (open && close && close > open) {
            p = open + 1;
        }
    }

    int r = -1, g = -1, b = -1;
    int color = DEFAULT_COLOR;

    if (swscanf_s(p, L"%d%*[, ]%d%*[, ]%d", &r, &g, &b) == 3) {
        r = Clamp(r, 0, 255);
        g = Clamp(g, 0, 255);
        b = Clamp(b, 0, 255);
        color = (r << 16) | (g << 8) | b;

        Wh_FreeStringSetting(value);
        return color;
    }

    if (*p == L'#') 
        ++p;

    bool hasHexAlpha = false;
    for (const wchar_t* t = p; *t; ++t) {
        if (iswspace(*t)) 
            break;
        if ((*t >= L'A' && *t <= L'F') || (*t >= L'a' && *t <= L'f')) {
            hasHexAlpha = true;
            break;
        }
    }

    int base = hasHexAlpha ? 16 : 0; // base=0 honors 0x for hex, otherwise decimal
    unsigned long parsed = wcstoul(p, nullptr, base);
    if (parsed > 0xFFFFFFUL) 
        parsed &= 0xFFFFFFUL;

    color = (int)parsed;

    Wh_FreeStringSetting(value);
    return color;
}

wstring Trim(const wstring& str) {
    size_t start = 0;
    while (start < str.size() && iswspace(str[start])) {
        start++;
    }
    size_t end = str.size();
    while (end > start && iswspace(str[end - 1])) {
        end--;
    }
    return str.substr(start, end - start);
}

template <typename... Args>
inline wstring GetStringSetting(PCWSTR valueName, Args... args) {
    return Trim( 
        wstring(
            WindhawkUtils::StringSetting::make(valueName, args...)
            .get()
        )
    );
}

HBRUSH TryCreateBrush(int rgb) {
    HBRUSH brush = CreateSolidBrush(ToColor(rgb));
    if (!brush) {
        Wh_Log(L"Failed to create %#x brush!", rgb);
        brush = CreateSolidBrush(ToColor(DEFAULT_COLOR));
    }

    if (!brush) {
        Wh_Log(L"Failed to create default brush!");
        return NULL;
    }

    return brush;
}

BOOL LoadSettings() {
    lock_t lock(g_cfgMutex);
    {
        g_cfg.aggressivePaint = Wh_GetIntSetting(L"GlobalSettings.aggressivePaint");
        g_cfg.longerPaint     = Wh_GetIntSetting(L"GlobalSettings.longerPaint");

        int    backColor = ParseColorSetting(L"GlobalSettings.backgroundColor");
        HBRUSH brush     = TryCreateBrush(backColor);

        if (!brush) {
            return FALSE;
        }

        g_cfg.brush = brush;
    }

    g_cfg.processes.clear();
    for (int i = 0;; i++) {
        auto name = GetStringSetting(L"ProcessesSettings[%d].name", i);
        if (name.empty()) {
            break;
        }

        g_cfg.processes[name].aggressivePaint =
            Wh_GetIntSetting(L"ProcessesSettings[%d].aggressivePaint", i);
        g_cfg.processes[name].longerPaint =
            Wh_GetIntSetting(L"ProcessesSettings[%d].longerPaint", i);
            
        int backColor = ParseColorSetting(L"ProcessesSettings[%d].backgroundColor", i);
        HBRUSH brush  = TryCreateBrush(backColor);

        if (brush)
            g_cfg.processes[name].brush = brush;
    }

    Wh_Log(L"Settings loaded");
    return FALSE;
}

// Hook rendering procedures
LRESULT WINAPI DefWindowProcA_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return FillWindow(hWnd, Msg, wParam, lParam, DefWindowProcA_Original);
}

LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return FillWindow(hWnd, Msg, wParam, lParam, DefWindowProcW_Original);
}

LRESULT WINAPI DefDlgProcA_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam){
    return FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcA_Original);
}

LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam){
    return FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original);
}


BOOL Wh_ModInit() {
    if (!LoadSettings()) {
        Wh_Log(L"Failed to load settings!");
        return FALSE;
    }

    using WindhawkUtils::SetFunctionHook;

    if (!SetFunctionHook(DefWindowProcA, DefWindowProcA_Hook, &DefWindowProcA_Original))
        Wh_Log(L"Failed to hook DefWindowProcA!");
    if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcA, DefDlgProcA_Hook, &DefDlgProcA_Original))
        Wh_Log(L"Failed to hook DefDlgProcA!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    Wh_Log(L">");
    return TRUE;
}

BOOL Wh_ModSettingsChanged(BOOL*) {
    if (!LoadSettings()) {
        Wh_Log(L"Failed to reload settings - unloading...");
        return FALSE;
    }

    return TRUE;
}

void Wh_ModUninit() {
    {
        lock_t lock(g_filledMutex);
        for (auto& win : g_filledWindows) {
            if (IsWindow(win.first)) {
                RedrawWindow(
                    win.first, NULL, NULL,
                    RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN
                );
            }
        }
        g_filledWindows.clear();
    }
    {
        lock_t lock(g_ownersMutex);
        g_windowsOwners.clear();
    }
    {
        lock_t lock(g_cfgMutex);
        if (g_cfg.brush)
            DeleteObject(g_cfg.brush);

        for (auto& win : g_cfg.processes) {
            if (win.second.brush) {
                DeleteObject(win.second.brush);
            }
        }
        g_cfg.processes.clear();
    }
}