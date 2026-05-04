// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.2
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

#include <windhawk_utils.h>

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

static HBRUSH g_windowBrush = CreateSolidBrush(0x00191919); // COLORREF: 0x00BBGGRR

static bool FillWindow(
    HWND hWnd,
    UINT Msg,
    WPARAM wParam,
    LPARAM lParam,
    DefProcCallback restore)
{
    // Fills the window with a rectangle, 
    // preventing it from being filled with a white background.
    
    if (Msg != WM_NCCALCSIZE)
        return false;
    
    // Skip children and already-visible windows
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    // if (style & WS_CHILD)
    if ((style & WS_CHILD) || (style & WS_VISIBLE))
        return false;

    // // Skip 1px areas
    RECT rect;
    if (!GetClientRect(hWnd, &rect)) 
        return false;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 1 || height <= 1)
        return false;

    if (wParam) {
        // Calc mode (dialogs)
        NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
        if (!params) return false;
        
        //  Call original FIRST to calc valid rects
        RECT origClient;
        CopyRect(&origClient, &params->rgrc[0]);
        DefWindowProcW_Original(hWnd, Msg, FALSE, (LPARAM)&origClient);  // Calc
        
        // Fill using orig rect (safe HDC now valid)
        HDC hdc = (HDC)wParam;  // May be valid post-orig
        FillRect(hdc, &origClient, g_windowBrush);
        
        // Copy calc'd client back (preserves NC/titlebar)
        CopyRect(&params->rgrc[0], &origClient);
        // WVR_VALIDRECTS implicit
    } else {  
        // Simple RECT (windows)
        RECT* rect = (RECT*)lParam;
        FillRect((HDC)wParam, rect, g_windowBrush);
    }

    return TRUE;
}

// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (FillWindow(hWnd, Msg, wParam, lParam, DefWindowProcW_Original))
        return TRUE;

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original))
        return TRUE;

    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);
}


BOOL Wh_ModInit()
{
    using WindhawkUtils::SetFunctionHook;

    if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit()
{
    if (g_windowBrush) {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }
}