// ==WindhawkMod==
// @id              fix-white-flash-test
// @name            Fix white flashes for all windows (test)
// @description     Fixes white flashes when opening new window.
// @version         0.2
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         notepad++.exe
// @include         xyplorer.exe
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

#include <unordered_map>
#include <windhawk_utils.h>

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

using DefProcCallback = LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM);

static const LRESULT FILL_ERROR = -1;
static std::unordered_map<HWND, bool> g_filledWindows;
static const HBRUSH BRUSH = CreateSolidBrush(0x00191919);

static bool ShouldSkip(HWND hWnd) {
    // if (g_filledWindows.contains(hWnd) && g_filledWindows.at(hWnd))
    //     return true;

    if (g_filledWindows.contains(hWnd)) {
        if (g_filledWindows.at(hWnd)) {
            Wh_Log(L"true");
            return true;
        }

        if (!g_filledWindows.at(hWnd))
            Wh_Log(L"false");
    }

    // Only top-level unrendered windows
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) /* || (style & WS_VISIBLE) */ || (exStyle & WS_EX_LAYERED))
        return true;

    return false;

}

static LRESULT FillWindow(
    HWND hWnd,
    UINT Msg,
    WPARAM wParam,
    LPARAM lParam,
    DefProcCallback restore)
{
    switch (Msg) {
    case WM_NCPAINT: {
        // This message usually appears first.
        // We're painting the non-client area first and 
        // then DefSubclassProc draws chrome elements 
        // (caption buttons, borders) on top of it.
        if (ShouldSkip(hWnd))
            return FILL_ERROR;
            
        HDC hdc = GetWindowDC(hWnd);  // includes NC
        if (!hdc) 
             return FILL_ERROR;

        RECT rect;
        if (!GetWindowRect(hWnd, &rect))
            return FILL_ERROR;

        rect = { 
            0, 0,  // left upper corner
            rect.right  - rect.left, 
            rect.bottom - rect.top          
        };

        FillRect(hdc, &rect, BRUSH);
        ReleaseDC(hWnd, hdc);
        Wh_Log(L"Paint rectangle %d (msg: 0x%04x)", hWnd, Msg);
        // The g_filledWindows flag is not set here because 
        // we've created the background but have not yet 
        // filled the window. 
        // The message below handles this.

        if (g_filledWindows.contains(hWnd))
            Wh_Log(L"filled: %d", g_filledWindows.at(hWnd));
        return restore(hWnd, Msg, wParam, lParam);
    }
    case WM_ERASEBKGND: {
        // Apply the rectangle fill and cover the
        // white background during window rendering.
        // It will be removed later so that the 
        // window elements become visible.
        if (ShouldSkip(hWnd))
            return FILL_ERROR;

        RECT rect;
        if (!GetClientRect(hWnd, &rect))
            return FILL_ERROR;

        // int width = rect.right - rect.left;
        // int height = rect.bottom - rect.top;
        // if (width <= 1 || height <= 1)
        //     return FILL_ERROR;

        // HDC hdc = GetWindowDC(hWnd);
        HDC hdc = GetDC(hWnd);
        if (hdc) {
            FillRect(hdc, &rect, BRUSH);
            ReleaseDC(hWnd, hdc);
            Wh_Log(L"Fill by hdc %d (msg: 0x%04x)", hWnd, Msg);
        } else if (wParam) {
            FillRect((HDC)wParam, &rect, BRUSH);
            Wh_Log(L"Fill by wParam %d (msg: 0x%04x)", hWnd, Msg);
        } else {
            InvalidateRect(hWnd, NULL, NULL);
            Wh_Log(L"Invalidate %d (msg: 0x%04x)", hWnd, Msg);
            // InvalidateRect(hWnd, &rect, TRUE);
            // return FILL_ERROR;
        }

        g_filledWindows[hWnd] = true;

        if (g_filledWindows.contains(hWnd))
            Wh_Log(L"filled: %d", g_filledWindows.at(hWnd));
        return restore(hWnd, Msg, wParam, lParam);
    }
    /* 
    case WM_ACTIVATE: {
        // Window is rendered, ensure cleanup
        // RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        // UpdateWindow(hWnd);
        auto it = g_filledWindows.find(hWnd);
        if (it != g_filledWindows.end()) {
            g_filledWindows.erase(it);
        }
        return FILL_ERROR;
    } 
    */
    } // switch

    return FILL_ERROR;
}


// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_NCCALCSIZE
     && FillWindow(hWnd, Msg, wParam, lParam, DefWindowProcW_Original)) {
        return TRUE;
    }

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original);
    if (result != FILL_ERROR) {
        return result;
    }

    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);
}


BOOL Wh_ModInit()
{
    using WindhawkUtils::SetFunctionHook;

    // if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
    //     Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit()
{
    if (BRUSH) {
        DeleteObject(BRUSH);
    }

    for (auto &win : g_filledWindows) {
        RedrawWindow(win.first, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    g_filledWindows.clear();
}