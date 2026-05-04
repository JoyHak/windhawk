// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.2
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         *
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

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

static HBRUSH g_windowBrush = CreateSolidBrush(0x00191919); // COLORREF: 0x00BBGGRR

static LRESULT FillWindow(
    HWND hWnd,
    UINT Msg,
    WPARAM wParam,
    LPARAM lParam,
    DefProcCallback restore
) {
    // Fills the window with a rectangle, 
    // preventing it from being filled with a white background.

    if (Msg != WM_NCCREATE || Msg != WM_CREATE)
        return -1;

    LRESULT res = restore(hWnd, Msg, wParam, lParam);
    HDC hdc = GetWindowDC(hWnd); // covers titlebar/frame + client
    if (hdc) {
        RECT wr;
        if (GetWindowRect(hWnd, &wr)) {
            int w = wr.right - wr.left;
            int h = wr.bottom - wr.top;
            RECT fill = {0,0,w,h}; // window-coordinates for GetWindowDC
            FillRect(hdc, &fill, g_windowBrush);
        }
        ReleaseDC(hWnd, hdc);
    }

    // Call original synchronously (never save/forward pointers)
    // Ensure frame re-paint
    RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);

    return res;
}

// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    LRESULT res = FillWindow(hWnd, Msg, wParam, lParam, DefWindowProcW_Original);
    if (res != -1)
        return res;

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // if (FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original))
    //     return TRUE;
    // return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);
    
    LRESULT res = FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original);
    if (res != -1)
        return res;
    
    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);  
}


BOOL Wh_ModInit()
{
    using WindhawkUtils::SetFunctionHook;

    // if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        // Wh_Log(L"Failed to hook DefWindowProcW!");
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