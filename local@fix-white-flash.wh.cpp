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

#include <mutex>
#include <unordered_map>
#include <windhawk_utils.h>

#define DCX_USESTYLE      0x00010000L

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

using DefProcCallback = LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM);

std::mutex g_filledMutex;
std::unordered_map<HWND, bool> g_filledWindows;      // prevents painting after full rendering
const HBRUSH BRUSH = CreateSolidBrush(0x00191919);   // represents color 0x00BBGGRR


// Helpers
static bool ShouldSkip(HWND hWnd) {
    std::lock_guard<std::mutex> lock(g_filledMutex);
    if (g_filledWindows.contains(hWnd))
        return true;

    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    if (g_filledWindows.contains(hRoot))
        return true;

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

static LRESULT FillWindow(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, DefProcCallback restore) {
    // Covers the white background with a colored rectangle while the window is rendering.
    switch (Msg) {
    case WM_NCPAINT: {
        // This message usually appears first 
        // and it's common for windows.
        // We're painting the non-client area first and 
        // then restore() draws chrome elements 
        // (caption buttons, borders) on top of it.
        if (ShouldSkip(hWnd))
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

        FillRect(hdc, &rect, BRUSH);
        ReleaseDC(hWnd, hdc);
        Wh_Log(L"Paint rectangle %d (msg: 0x%04x)", hWnd, Msg);
        // TODO: "paint on resize/agressive painting" checkbox
        
        HWND hRoot = GetAncestor(hWnd, GA_ROOT);

        std::lock_guard<std::mutex> lock(g_filledMutex);
        g_filledWindows[hRoot] = true;
        g_filledWindows[hWnd]  = true;
        break;
    }
    case WM_ERASEBKGND: {
        // This message is common for dialogs.
        // Apply the rectangle fill and cover the
        // white background during window rendering.
        // It will be removed later so that the 
        // window elements become visible.
        if (ShouldSkip(hWnd))
            break;

        RECT rect;
        if (!GetClientRect(hWnd, &rect))
            break;

        HDC hdc = (HDC)wParam;
        if (!hdc) {
            break;
        }

        FillRect(hdc, &rect, BRUSH);
        ReleaseDC(hWnd, hdc);

        HWND hRoot = GetAncestor(hWnd, GA_ROOT);

        std::lock_guard<std::mutex> lock(g_filledMutex);
        g_filledWindows[hRoot] = true;
        g_filledWindows[hWnd]  = true;
        // break;
        return TRUE; // background erased - don't let the original erase it again
    }
    case WM_ACTIVATEAPP: {
        // Window is rendered, don't paint again
        
        HWND hRoot = GetAncestor(hWnd, GA_ROOT);

        std::lock_guard<std::mutex> lock(g_filledMutex);
        g_filledWindows[hRoot] = true;
        g_filledWindows[hWnd] = true;
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


// Init mod
BOOL Wh_ModInit() {
    using WindhawkUtils::SetFunctionHook;

    if (!SetFunctionHook(DefWindowProcA, DefWindowProcA_Hook, &DefWindowProcA_Original))
        Wh_Log(L"Failed to hook DefWindowProcA!");
    if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcA, DefDlgProcA_Hook, &DefDlgProcA_Original))
        Wh_Log(L"Failed to hook DefDlgProcA!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit() {
    if (BRUSH) {
        DeleteObject(BRUSH);
    }

    std::lock_guard<std::mutex> lock(g_filledMutex);
    for (auto &win : g_filledWindows) {
        RedrawWindow(win.first, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    g_filledWindows.clear();
}