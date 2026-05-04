// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.2
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         notepad++.exe
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

struct CallbackArgs {
    HWND hWnd;
    UINT Msg;
    WPARAM wParam;
    LPARAM lParam;

    // called by timer to run original handler
    DefProcCallback restore; 
};

static std::unordered_map<UINT_PTR, CallbackArgs> g_filledWindows;
static UINT_PTR g_timerCounter = 1;
static HBRUSH g_windowBrush = CreateSolidBrush(0x00191919); // COLORREF: 0x00BBGGRR

VOID CALLBACK RestoreWindow(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/)
{
    // Restores the window's contents by calling its original procedure.
    auto it = g_filledWindows.find(idEvent);
    if (it == g_filledWindows.end())
        return;

    CallbackArgs args = it->second;
    // args.restore(args.hWnd, args.Msg, args.wParam, args.lParam);
    args.restore(args.hWnd, WM_NCCALCSIZE, args.wParam, args.lParam);
    args.restore(args.hWnd, WM_MOVE, args.wParam, args.lParam);
    args.restore(args.hWnd, WM_ERASEBKGND, args.wParam, args.lParam);

    // Force non-client repaint so titlebar/frame appear immediately.
    // RDW_FRAME invalidates the frame; RDW_UPDATENOW causes immediate paint.
    // RedrawWindow(args.hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    UpdateWindow(args.hWnd);

    if (KillTimer(args.hWnd, idEvent))
        g_filledWindows.erase(it);
}

static bool FillWindow(
    HWND hWnd,
    UINT Msg,
    WPARAM wParam,
    LPARAM lParam,
    DefProcCallback restore)
{
    // Fills the window with a rectangle, 
    // preventing it from being filled with a white background.

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

    HDC hdc = GetWindowDC(hWnd);
    if (hdc) {
        FillRect(hdc, &rect, g_windowBrush);
        ReleaseDC(hWnd, hdc);
    } else if (wParam) {
        FillRect((HDC)wParam, &rect, g_windowBrush);
    } else {
        return false;
    }

    // InvalidateRect(hWnd, NULL, NULL);

    // Schedule restore
    UINT_PTR timerId = ++g_timerCounter;
    g_filledWindows[timerId] = { hWnd, Msg, wParam, lParam, restore };
    SetTimer(hWnd, timerId, USER_TIMER_MINIMUM, RestoreWindow);

    return true;
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
    if (Msg == WM_NCCALCSIZE
     && FillWindow(hWnd, Msg, wParam, lParam, DefDlgProcW_Original)) {
        // return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);
        return TRUE;
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
    if (g_windowBrush) {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }

    for (auto &p : g_filledWindows) {
        KillTimer(p.second.hWnd, p.first);
    }

    g_filledWindows.clear();
}