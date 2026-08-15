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
  $name: Settings per Process
  $description: >-
    You can set individual parameters for each process. 
    Click "Add new item" below to add a new process.
*/
// ==/WindhawkModSettings==


#include <mutex>
#include <unordered_map>
#include <windhawk_utils.h>

#define DCX_USESTYLE      0x00010000L


// Settings
struct StringSetting {
    PCWSTR value;
    
    StringSetting(PCWSTR name, ...) {
        va_list args;
        va_start(args, name);
        value = Wh_GetStringSetting(name, args);
        va_end(args);
    }

    ~StringSetting() { 
        if (value) 
            Wh_FreeStringSetting(value); 
    }

    operator PCWSTR() const { 
        return value; 
    }
};

struct ProcessData {
    DWORD processId = 0;
    DWORD threadId  = 0;
    PCWSTR name;
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
    std::unordered_map<PCWSTR, ProcessSettings> processes;
} g_cfg;
std::mutex g_cfgMutex;

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

using DefProcCallback = LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM);

std::mutex g_filledMutex;
std::unordered_map<HWND, bool> g_filledWindows;      // prevents painting after full rendering

std::mutex g_ownersMutex;
std::unordered_map<HWND, ProcessData> g_windowsOwners;   // holds cache for windows


// Helpers
bool ShouldSkip(HWND hWnd, bool aggressivePaint) {
    std::lock_guard<std::mutex> lock(g_filledMutex);
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
    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    
    std::lock_guard<std::mutex> lock(g_filledMutex);
    g_filledWindows[hRoot] = true;
    g_filledWindows[hWnd]  = true;    
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
    int g = (rgb >> 8) & 0xFF;
    int b = rgb & 0xFF;

    return RGB(r, g, b);
}

PCWSTR GetProcessName(HWND hWnd) {
    DWORD ownerPid = 0;
    const DWORD ownerTid = GetWindowThreadProcessId(hWnd, &ownerPid);
    {
        std::lock_guard<std::mutex> lock(g_ownersMutex);
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

    PCWSTR procName = L"";
    WCHAR exePath[MAX_PATH * 4] = { 0 };
    DWORD exePathLen = _countof(exePath);

    if (QueryFullProcessImageNameW(hProc, 0, exePath, &exePathLen)) {
        WCHAR* base = wcsrchr(exePath, L'\\');
        if (base) {
            base++; // points to filename
            // duplicate so we can modify (strip extension, lowercase)
            wchar_t* dup = _wcsdup(base);
            if (dup) {
                // strip extension
                wchar_t* dot = wcschr(dup, L'.');
                if (dot) 
                    *dot = L'\0';

                for (wchar_t* p = dup; *p; ++p) 
                    *p = towlower(*p);

                procName = dup; // owned by us
            }
        }
    }

    CloseHandle(hProc);

    if (procName) {
        std::lock_guard<std::mutex> lock(g_ownersMutex);
        g_windowsOwners[hWnd] = ProcessData { ownerPid, ownerTid, procName };
        // return pointer stored in the map to ensure stable lifetime
        return g_windowsOwners[hWnd].name;
    }

    return L"";
}

ProcessSettings GetProcessSettings(HWND hWnd) {
    PCWSTR name = GetProcessName(hWnd);
    if (name) {
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
    case WM_ACTIVATEAPP: 
    case WM_INITDIALOG:
    case WM_ENTERSIZEMOVE:
    {
        // Top-level window is rendered, don't paint again
        LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
        if ((style & WS_CHILD) 
        || !(style & WS_CAPTION) 
        || !(style & WS_THICKFRAME)) {
            MarkToSkip(hWnd);
        }

        break;
    }
    case WM_NCDESTROY: {
        // Window is destroyed, forget the flag
        {
            std::lock_guard<std::mutex> lock(g_filledMutex);
            auto it = g_filledWindows.find(hWnd);
            if (it != g_filledWindows.end()) {
                g_filledWindows.erase(it);
            }
        }
        {
            HWND hRoot = GetAncestor(hWnd, GA_ROOT);
            std::lock_guard<std::mutex> lock(g_filledMutex);
            auto it = g_filledWindows.find(hRoot);
            if (it != g_filledWindows.end()) {
                g_filledWindows.erase(it);
            }
        }
        break;
    }
    } // switch

    return restore(hWnd, Msg, wParam, lParam);
}


// Initial stage
int ParseColorSetting(PCWSTR value) {
    int color = 0x191919;
    if (!value) 
        return color;

    const wchar_t* p = value;
    while (*p && iswspace(*p)) 
        ++p;

    if (!*p) {
        return color;
    }

    if (wcsncmp(p, L"rgb", 3) == 0) {
        const wchar_t* open  = wcschr(p, L'(');
        const wchar_t* close = wcschr(p, L')');
        if (open && close && close > open) {
            p = open + 1;
        }
    }

    int r = -1, g = -1, b = -1;

    if (swscanf_s(p, L"%d%*[, ]%d%*[, ]%d", &r, &g, &b) == 3) {
        r = Clamp(r, 0, 255);
        g = Clamp(g, 0, 255);
        b = Clamp(b, 0, 255);
        color = (r << 16) | (g << 8) | b;
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
    return color;
}

BOOL LoadSettings() {
    std::lock_guard<std::mutex> lock(g_cfgMutex);
    {
        g_cfg.aggressivePaint = Wh_GetIntSetting(L"aggressivePaint");
        g_cfg.longerPaint     = Wh_GetIntSetting(L"longerPaint");

        StringSetting sBackColor(L"GlobalSettings.backgroundColor");
        int iBackColor = ParseColorSetting(sBackColor);

        HBRUSH brush = CreateSolidBrush(
            ToColor(iBackColor)
        );

        if (!brush) {
            Wh_Log(L"Failed to create brush for %s!", sBackColor.value);
            brush = CreateSolidBrush(0x00191919);
        }

        if (!brush) {
            Wh_Log(L"Failed to create default brush!");
            return FALSE;
        }
        g_cfg.brush = brush;
    }

    g_cfg.processes.clear();
    for (int i = 0;; i++) {
        StringSetting name(L"ProcessesSettings[%d].name", i);
        if (!*name) {
            break;
        }

        g_cfg.processes[name].aggressivePaint =
            Wh_GetIntSetting(L"ProcessesSettings[%d].aggressivePaint", i);
        g_cfg.processes[name].longerPaint =
            Wh_GetIntSetting(L"ProcessesSettings[%d].longerPaint", i);
            
        StringSetting sBackColor(L"ProcessesSettings[%d].backgroundColor", i);
        int iBackColor = ParseColorSetting(sBackColor);

        HBRUSH brush = CreateSolidBrush(
            ToColor(iBackColor)
        );

        if (!brush) {
            Wh_Log(L"Failed to create %s brush for %s!", sBackColor.value, name.value);
            brush = CreateSolidBrush(0x00191919);
        }

        if (!brush) {
            Wh_Log(L"Failed to create default brush for %s!", name.value);
            continue;
        }

        g_cfg.processes[name].brush = brush;
    }

    Wh_Log(L"Settings loaded");
    return TRUE;
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
        std::lock_guard<std::mutex> lock(g_filledMutex);
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
        std::lock_guard<std::mutex> lock(g_ownersMutex);
        g_windowsOwners.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);
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