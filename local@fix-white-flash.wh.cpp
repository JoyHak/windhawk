// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.3
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         notepad++.exe
// @include         xyplorer.exe
// @compilerOptions -lGdi32 -lComctl32
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

#include <windhawk_utils.h>
#include <commctrl.h>       // SetWindowSubclass, DefSubclassProc
#include <unordered_map>    // sub-classed windows


decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

static const UINT_PTR WH_SUBCLASS_ID = 0xFEEDBEEF;
static const HBRUSH BRUSH = CreateSolidBrush(0x00191919);     // COLORREF: 0x00BBGGRR
static std::unordered_map<HWND, bool> g_subclassedWindows;


static void RemoveWindowFiller(HWND hWnd, UINT_PTR uIdSubclass);

LRESULT CALLBACK FillWindow(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/
) {
    switch (uMsg) {
    case WM_NCPAINT: {
        // paint non-client once if not done yet
        if (g_subclassedWindows[hWnd])
            break;
            
        HDC hdc = GetWindowDC(hWnd);  // includes NC
        if (!hdc) 
             break;

        RECT rect;
        if (!GetWindowRect(hWnd, &rect))
            break;

        rect = { 
            0, 0,  // left upper corner
            rect.right  - rect.left, 
            rect.bottom - rect.top          
        };

        FillRect(hdc, &rect, g_windowBrush);
        ReleaseDC(hWnd, hdc);
        Wh_Log(L"Paint %d (msg: 0x%04x)", hWnd, uMsg);
        // do not set prefilled here necessarily; 
        // we wait for WM_ERASEBKGND/WM_PAINT too.
 
        // allow default NC painting to draw chrome on top
        break;
    }
    case WM_ERASEBKGND: {
        // Only prefill if we haven't yet
        if (g_subclassedWindows[hWnd])
            break;

        RECT rect;
        if (!GetClientRect(hWnd, &rect))
            break;

        // int width = rect.right - rect.left;
        // int height = rect.bottom - rect.top;
        // if (width <= 1 || height <= 1)
        //     break;

        // HDC hdc = GetWindowDC(hWnd);
        HDC hdc = GetDC(hWnd);
        if (hdc) {
            FillRect(hdc, &rect, g_windowBrush);
            ReleaseDC(hWnd, hdc);
            Wh_Log(L"Fill with hdc %d (msg: 0x%04x)", hWnd, uMsg);
        } else if (wParam) {
            FillRect((HDC)wParam, &rect, g_windowBrush);
            Wh_Log(L"Fill with wParam %d (msg: 0x%04x)", hWnd, uMsg);
        } else {
            InvalidateRect(hWnd, NULL, NULL);
            Wh_Log(L"Invalidate %d (msg: 0x%04x)", hWnd, uMsg);
            // InvalidateRect(hWnd, &rect, TRUE);
            // break;
        }

        // Indicate that we've handled erase
        g_subclassedWindows[hWnd] = true;
        return 1;
    }

    case WM_PAINT: {
        // Let the window perform its normal paint, 
        // then remove our rectangle
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        RemoveWindowFiller(hWnd, uIdSubclass);
        return result;
    }

    case WM_NCDESTROY: {
        // Window is going away, ensure cleanup
        RemoveWindowFiller(hWnd, uIdSubclass);
        break;
    }
    } // switch

    // Perform window rendering
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Helpers
static void SetWindowFiller(HWND hWnd) {
    // Only top-level non-layered windows
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) || (exStyle & WS_EX_LAYERED))
        return;

    // Avoid double-install
    if (g_subclassedWindows.find(hWnd) != g_subclassedWindows.end())
        return;

    // Install subclass with state in dwRefData
    BOOL ok = SetWindowSubclass(
        hWnd, FillWindow, WH_SUBCLASS_ID,
        reinterpret_cast<DWORD_PTR>(0)
    );

    if (ok) {
        g_subclassedWindows[hWnd] = false;
        // Wh_Log(L"Install %p %x", hWnd, WH_SUBCLASS_ID);
    }
}

static void RemoveWindowFiller(HWND hWnd, UINT_PTR uIdSubclass) {
    // Prevent double-cleaning in the future
    auto it = g_subclassedWindows.find(hWnd);
    if (it == g_subclassedWindows.end()) {
        return;
    } else {
        g_subclassedWindows.erase(it);
    }

    RemoveWindowSubclass(hWnd, FillWindow, uIdSubclass);
    // Wh_Log(L"Remove %d %x", hWnd, uIdSubclass);
}

// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_NCCREATE || Msg == WM_CREATE)
        SetWindowFiller(hWnd);

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);   
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // if (Msg == WM_INITDIALOG)
    if (Msg == WM_NCCREATE || Msg == WM_CREATE)
        SetWindowFiller(hWnd);

    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);  
}


// Initialize the mod
BOOL Wh_ModInit() {
    using WindhawkUtils::SetFunctionHook;

    if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit() {
    if (g_windowBrush) {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }

    for (auto &win : g_subclassedWindows) {
        RemoveWindowSubclass(win.first, FillWindow, WH_SUBCLASS_ID);
    }
    g_subclassedWindows.clear();
}
