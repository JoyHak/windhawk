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

#include <windhawk_utils.h>
#include <commctrl.h>  // SetWindowSubclass, DefSubclassProc

decltype(&DefWindowProcA) DefWindowProcA_Original = nullptr;
decltype(&DefWindowProcW) DefWindowProcW_Original = nullptr;
decltype(&DefDlgProcA)    DefDlgProcA_Original    = nullptr;
decltype(&DefDlgProcW)    DefDlgProcW_Original    = nullptr;

static const UINT_PTR WH_SUBCLASS_ID = 0xFEEDBEEF;
static HBRUSH g_windowBrush = CreateSolidBrush(0x00191919); // COLORREF: 0x00BBGGRR

// Per-window state stored in subclass refData
struct SubclassState {
    bool prefilled;    // we already did the prefill
    bool removed;      // subclass removed (to avoid double-free)
};

/* 
// Helper: detect layered / DWM-composited windows we should skip
static bool IsWindowSkippable(HWND hWnd)
{
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    // LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    return ((style & WS_CHILD)); 
    //    || (style & WS_VISIBLE)
    //    || (exStyle & WS_EX_LAYERED))
}
 */

LRESULT CALLBACK FillWindow(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData
) {
    SubclassState* state = reinterpret_cast<SubclassState*>(dwRefData);
    if (!state) {
        // should not happen, but fall back to default
        Wh_Log(L"Fallback %d (msg: 0x%04x)", hWnd, uMsg);
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
    case WM_ERASEBKGND: {
        // Only prefill if we haven't yet
        if (state->prefilled)
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

        state->prefilled = true;
        // indicate we've handled erase
        return 1;
    }

    case WM_NCPAINT: {
        // paint non-client once if not done yet
        if (state->prefilled)
            break;
            
        HDC hdc = GetWindowDC(hWnd);  // includes NC
        if (!hdc) 
             break;

        RECT rect;
        if (!GetClientRect(hWnd, &rect))
            break;

        RECT fill = { 
            0, 0,  // left upper corner
            rect.right  - rect.left, 
            rect.bottom - rect.top          
        };

        FillRect(hdc, &fill, g_windowBrush);
        ReleaseDC(hWnd, hdc);
        Wh_Log(L"Paint %d (msg: 0x%04x)", hWnd, uMsg);
        // do not set prefilled here necessarily; 
        // we wait for WM_ERASEBKGND/WM_PAINT too.

        // allow default NC painting to draw chrome on top
        break;
    }

    case WM_PAINT: {
        // When first WM_PAINT happens, allow it to run and then uninstall the subclass.
        // We call DefSubclassProc to let the app paint; then remove subclass and free state.
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);

        if (!state->removed) {
            // remove subclass now we no longer need to intercept erases
            RemoveWindowSubclass(hWnd, FillWindow, uIdSubclass);
            state->removed = true;
            delete state; // free allocated state
            Wh_Log(L"Remove %d %x (msg: 0x%04x)", hWnd, uIdSubclass, uMsg);
        }
        return result;
    }

    case WM_NCDESTROY: {
        // Clean up if not already removed
        if (!state->removed) {
            RemoveWindowSubclass(hWnd, FillWindow, uIdSubclass);
            state->removed = true;
            delete state;
            Wh_Log(L"Destroy %d %x (msg: 0x%04x)", hWnd, uIdSubclass, uMsg);
        }
        break;
    }
    } // switch

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SetWindowFiller(HWND hWnd) {
    // Basic filters: only top-level non-layered windows
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) || (exStyle & WS_EX_LAYERED))
        return;

    // Allocate per-window state
    SubclassState* state = new SubclassState();
    state->prefilled = false;
    state->removed   = false;

    // Install subclass with state in dwRefData
    SetWindowSubclass(
        hWnd, FillWindow, WH_SUBCLASS_ID, 
        reinterpret_cast<DWORD_PTR>(state)
    );
    Wh_Log(L"Install %d %x", hWnd, WH_SUBCLASS_ID);
}

// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_NCCREATE || Msg == WM_CREATE)
        SetWindowFiller(hWnd);

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);   
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_INITDIALOG)
        SetWindowFiller(hWnd);

    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);  
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
    
    // May cause memory leak here because we're not calling `delete` on
    // remained heap allocated SubclassState structs
}