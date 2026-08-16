// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.1
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include explorer.exe
// @include *
// @exclude csrss.exe
// @exclude dwm.exe
// @exclude winlogon.exe
// @exclude services.exe
// @exclude svchost.exe
// @exclude lsass.exe
// @exclude smss.exe
// @exclude wininit.exe
// @exclude conhost.exe
// @exclude fontdrvhost.exe
// @exclude audiodg.exe
// @exclude wmic.exe
// @exclude wmiapsrv.exe
// @exclude wmiprvse.exe
// @exclude alg.exe
// @exclude nvcplui.exe
// @exclude nvcontainer.exe
// @exclude git.exe
// @exclude windhawk-cli.exe
// @exclude clang++.exe
// @exclude clang-20.exe
// @exclude ld.lld.exe
// @exclude TextInputHost.exe
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

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

// == Helpers ==

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

/**
 * @brief Trims spaces, tabs and newlines. 
 * Neccessary function to avoid accidental spaces around process name.
 */
wstring Trim(const wstring& s) {
    // trims spaces, tabs and newlines
    size_t start = 0;
    while (start < s.size() 
        && (s[start] == L' ' 
        || s[start] == L'\t' 
        || s[start] == L'\n')) {
        ++start;
    }

    size_t end = s.size();
    while (end > start 
    && (s[end - 1] == L' ' 
        || s[end - 1] == L'\t' 
        || s[end - 1] == L'\n')) {
        --end;
    }

    return s.substr(start, end - start);
}

// == Cache ==

struct ProcessData {
    DWORD processId = 0;
    DWORD threadId  = 0;
    wstring name;
};

std::mutex g_cacheMutex;
std::unordered_map<HWND, ProcessData> g_cachedWindows;

/**
* @brief Queries name of the process by window handle
* and stores it in the cache to avoid multiple syscalls.
*/
wstring GetProcessName(HWND hWnd) {
    DWORD ownerPid = 0;
    const DWORD ownerTid = GetWindowThreadProcessId(hWnd, &ownerPid);
    {
        lock_t lock(g_cacheMutex);
        auto it = g_cachedWindows.find(hWnd);

        if (it != g_cachedWindows.end()
         && it->second.processId == ownerPid
         && it->second.threadId  == ownerTid) {
            return it->second.name;
        }

        if (it != g_cachedWindows.end()) 
            g_cachedWindows.erase(it);
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
        lock_t lock(g_cacheMutex);
        g_cachedWindows[hWnd] = ProcessData { ownerPid, ownerTid, procName };
        // return pointer stored in the map to ensure stable lifetime
        return g_cachedWindows[hWnd].name;
    }

    return L"";
}

// == Data ==

/**
 * @brief Holds marks that window and its root ancestor 
 * are rendered (skip painting).
 * Can be checked via `SkipWin::Skip(hWnd)`
 */
class SkipWin {
  public:
    SkipWin() = delete;
    SkipWin(const SkipWin&) = delete;
    SkipWin& operator=(const SkipWin&) = delete;
    ~SkipWin() = delete;

    static void Mark(HWND hWnd) {
        if (!hWnd) 
            return;

        lock_t lock(s_mutex);
        s_windows[hWnd] = true;

        HWND root = GetAncestor(hWnd, GA_ROOT);
        if (root)
            s_windows[root] = true;
    }

    static void Unmark(HWND hWnd) {
        if (!hWnd) 
            return;

        lock_t lock(s_mutex);
        s_windows.erase(hWnd);

        HWND root = GetAncestor(hWnd, GA_ROOT); 
        if (root)
            s_windows.erase(root);
    }

    static bool Skip(HWND hWnd, bool aggressivePaint = true) {
        if (!hWnd) 
            return true;

        {
            lock_t lock(s_mutex);  
            if (s_windows.find(hWnd) != s_windows.end()) 
                return true;

            HWND root = GetAncestor(hWnd, GA_ROOT);
            if (root && s_windows.find(root) != s_windows.end()) 
                return true;
        }

        if (aggressivePaint) 
            return false;

        LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
        if (!(style & WS_CAPTION) || !(style & WS_THICKFRAME)) 
            return true;

        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        if ((style & WS_CHILD) || (exStyle & WS_EX_LAYERED)) 
            return true;

        return false;
    }

    /**
    * @brief Clears all marks and redraws previously-marked windows
    */
    static void Clear() {
        lock_t lock(s_mutex);

        for (auto& win : s_windows) {
            if (IsWindow(win.first)) {
                RedrawWindow(
                    win.first, NULL, NULL,
                    RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN
                );
            }
        }
        s_windows.clear();
    }

  private:
    // inline = declaration also a definition
    // https://stackoverflow.com/a/46874207
    inline static std::mutex s_mutex;
    inline static std::unordered_map<HWND, bool> s_windows;
};

/**
 * @brief RAII wrapper for settings and brushes.
 * Stores user settings in the single instance (fully static).
 */
class Cfg {
  public:
    Cfg() = delete;
    Cfg(const Cfg&) = delete;
    Cfg& operator=(const Cfg&) = delete;
    ~Cfg() = delete;

    /**
    * @brief Available user settings.
    */
    struct Values {
        HBRUSH brush;
        bool aggressivePaint;
        bool longerPaint;
    };

    /**
    * @brief Loads all settings. Returns true on success.
    */
    static bool Load() {
        Unload(); // safe cleanup
        lock_t lock(s_mutex);

        s_global.aggressivePaint = Wh_GetIntSetting(L"GlobalSettings.aggressivePaint");
        s_global.longerPaint     = Wh_GetIntSetting(L"GlobalSettings.longerPaint");
        {
            int    backColor = ParseColor(L"GlobalSettings.backgroundColor");
            HBRUSH brush     = TryCreateBrush(backColor);

            if (!brush)
                return false;

            s_global.brush = brush;
            Wh_Log(L"Color: %#x, Brush: %#x", backColor, s_global.brush);
        }

        for (int i = 0;; ++i) {
            auto name = GetString(L"ProcessesSettings[%d].name", i);
            Wh_Log(L"'%s'", name.c_str());
            if (name.empty()) 
                break;

            s_processes[name].aggressivePaint = 
                Wh_GetIntSetting(L"ProcessesSettings[%d].aggressivePaint", i);
            s_processes[name].longerPaint = 
                Wh_GetIntSetting(L"ProcessesSettings[%d].longerPaint", i);

            int    backColor = ParseColor(L"ProcessesSettings[%d].backgroundColor");
            HBRUSH brush     = TryCreateBrush(backColor);

            if (brush)
                s_processes[name].brush = brush;

            Wh_Log(L"'%s' a=%d l=%d", name.c_str(), s_processes[name].aggressivePaint, s_processes[name].longerPaint);
            Wh_Log(L"Color: %#x, Brush: %#x", backColor, s_processes[name].brush);
        }

        Wh_Log(L"Settings loaded");
        return false;
    }

    /**
    * @brief Frees brushes and clears maps.
    */
    static void Unload() {
        lock_t lock(s_mutex);

        if (s_global.brush) { 
            DeleteObject(s_global.brush);
        }

        for (auto &kv : s_processes) {
            if (kv.second.brush) { 
                DeleteObject(kv.second.brush); 
            }
        }

        s_processes.clear();
    }

    /**
    * @brief Syntax sugar to get correct values.
    */
    static Values Get(HWND hWnd) {
        wstring name = GetProcessName(hWnd);
        if (!name.empty()) {
            auto it = s_processes.find(name);
            if (it != s_processes.end())
                return it->second;
        }

        return s_global;
    }

private:
    /**
    * @brief RAII wrapper for a string setting. 
    * Trims leading and trailing spaces.
    */
    template <typename... Args>
    static wstring GetString(PCWSTR valueName, Args... args) {
        PCWSTR p = WindhawkUtils::StringSetting::make(valueName, args...).get();
        if (!p || !*p) 
            return {};

        return Trim(wstring(p));
    }

    /**
    * @brief Parses color from user: RGB(0,0,0); 0x0; #0
    * @returns Integer that represents RGB (not COLORREF!).
    */
    template <typename... Args>
    static int ParseColor(PCWSTR valueName, Args... args) {
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

    /**
    * @brief Creates solid brush with fall back to DEFAULT_COLOR
    */
    static HBRUSH TryCreateBrush(int rgb) {
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

    // inline = declaration also a definition
    // https://stackoverflow.com/a/46874207
    inline static std::mutex s_mutex;
    inline static Values s_global{};
    inline static std::unordered_map<wstring, Values> s_processes;
};

// == Main ==

/**
* @brief Covers the white background with a colored rectangle 
* while the window is rendering.
*/
LRESULT FillWindow(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, DefProcCallback restore) {
    switch (Msg) {
    case WM_NCPAINT: {
        // This message usually appears first 
        // and it's common for windows.
        // We're painting the non-client area first and 
        // then restore() draws chrome elements 
        // (caption buttons, borders) on top of it.
        auto cfg = Cfg::Get(hWnd);
        if (SkipWin::Skip(hWnd, cfg.aggressivePaint))
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
            SkipWin::Mark(hWnd);

        break;
    }
    case WM_ERASEBKGND: {
        // This message is common for dialogs.
        // Apply the rectangle fill and cover the
        // white background during window rendering.
        // It will be removed later so that the 
        // window elements become visible.
        auto cfg = Cfg::Get(hWnd);
        if (SkipWin::Skip(hWnd, cfg.aggressivePaint))
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
        SkipWin::Mark(hWnd);

        return TRUE; // background erased - don't let the original erase it again
    } 
    case WM_SETCURSOR:
    case WM_ENTERSIZEMOVE: {
        // Window is rendered, don't paint again
        SkipWin::Mark(hWnd);
        break;
    }
    case WM_NCDESTROY: {
        // Window is destroyed, allow painting later
        SkipWin::Unmark(hWnd);
        break;
    }
    } // switch

    return restore(hWnd, Msg, wParam, lParam);
}

// == Hook rendering procedures ==

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
    if (!Cfg::Load()) {
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
    if (!Cfg::Load()) {
        Wh_Log(L"Failed to reload settings - unloading...");
        return FALSE;
    }

    return TRUE;
}

void Wh_ModUninit() {
    Cfg::Unload();
    SkipWin::Clear();
    {
        lock_t lock(g_cacheMutex);
        g_cachedWindows.clear();
    }
}