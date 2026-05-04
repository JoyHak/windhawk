// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.3
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         notepad++.exe
// @include         explorer.exe
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h> // SetWindowSubclass / DefSubclassProc
// #include <dwmapi.h>

static const UINT_PTR WH_SUBCLASS_ID = 0xFEEDBEEF;

// Helper: detect layered / DWM-composited windows we should skip
static bool IsWindowSkippable(HWND hWnd) {
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    // DWM check (requires -ldwmapi.lib)
    // BOOL isComposition = FALSE;
    // DwmIsCompositionEnabled(&isComposition);
    // if (isComposition)

    return ((style & WS_CHILD)); 
         || (style & WS_VISIBLE)
         || (exStyle & WS_EX_LAYERED))
         
}

LRESULT CALLBACK FillWindow(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/
) {
    switch (uMsg) {
    case WM_ERASEBKGND: {
        if (IsWindowSkippable(hWnd))
            break;

        // wParam is HDC for WM_ERASEBKGND (may be NULL)
        HDC hdc = (HDC)wParam;
        BOOL needRelease = FALSE;
        if (!hdc) { hdc = GetDC(hWnd); needRelease = TRUE; }

        RECT rc;
        if (GetClientRect(hWnd, &rc))
            FillRect(hdc, &rc, g_windowBrush);

        if (needRelease) ReleaseDC(hWnd, hdc);

        // Indicate we handled erase background
        return 1;
    }

    case WM_NCPAINT: {
        if (IsWindowSkippable(hWnd))
            break;

        // Non-client paint: get full window DC and paint the whole window rect.
        HDC hdc = GetWindowDC(hWnd); // includes non-client
        if (hdc) {
            RECT wr;
            if (GetWindowRect(hWnd, &wr)) {
                int w = wr.right - wr.left;
                int h = wr.bottom - wr.top;
                RECT fill = { 0, 0, w, h };
                FillRect(hdc, &fill, g_windowBrush);
            }
            ReleaseDC(hWnd, hdc);
        }

        // Returning 0 might be fine — we've painted non-client. Some classes expect defproc.
        // Let the default non-client paint still run to draw frame elements on top.
        // So fall through to DefSubclassProc below instead of returning immediately.
        break;
    }

    case WM_NCDESTROY:
        // Clean up subclass entry
        RemoveWindowSubclass(hWnd, FillWindow, uIdSubclass);
        break;
    }

    // Default behavior for everything else
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_NCCREATE || Msg == WM_CREATE)
        SetWindowSubclass(hWnd, FillWindow, WH_SUBCLASS_ID, 0);

    // Forward message normally
    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam)
}

LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_INITDIALOG)
        SetWindowSubclass(hWnd, FillWindow, WH_SUBCLASS_ID, 0);

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam)
}


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
}