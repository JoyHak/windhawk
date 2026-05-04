// ==WindhawkMod==
// @id              fix-white-flash
// @name            Fix white flashes for all windows
// @description     Fixes white flashes when opening new window.
// @version         0.3
// @author          Rafaello
// @github          https://github.com/JoyHak
// @include         notepad++.exe
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

static const UINT_PTR     SUBCLASS_ID             = 0xFEEDBEEF;
static const HBRUSH       BRUSH                   = CreateSolidBrush(0x00191919); // 0x00BBGGRR

static std::unordered_map<HWND, bool> g_filledWindows;    // prevent any double fill/remove actions


static void RemoveWindowFiller(HWND hWnd, UINT_PTR uIdSubclass);

LRESULT CALLBACK FillWindow(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/
) {
    switch (uMsg) {
    case WM_NCPAINT: {
        // This message usually appears first.
        // We're painting the non-client area first and 
        // then DefSubclassProc draws chrome elements 
        // (caption buttons, borders) on top of it.
        if (g_filledWindows[hWnd])
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

        FillRect(hdc, &rect, BRUSH);
        ReleaseDC(hWnd, hdc);
        Wh_Log(L"Paint %d (msg: 0x%04x)", hWnd, uMsg);

        // The g_filledWindows flag is not set here because 
        // we've created the background but have not yet 
        // filled the window. 
        // The message below handles this.
        break;
    }
    case WM_ERASEBKGND: {
        // Apply the rectangle fill and cover the
        // white background during window rendering.
        // It will be removed later so that the 
        // window elements become visible.
        if (g_filledWindows[hWnd])
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
            FillRect(hdc, &rect, BRUSH);
            ReleaseDC(hWnd, hdc);
            Wh_Log(L"Fill by hdc %d (msg: 0x%04x)", hWnd, uMsg);
        } else if (wParam) {
            FillRect((HDC)wParam, &rect, BRUSH);
            Wh_Log(L"Fill by wParam %d (msg: 0x%04x)", hWnd, uMsg);
        } else {
            InvalidateRect(hWnd, NULL, NULL);
            // InvalidateRect(hWnd, &rect, TRUE);
            Wh_Log(L"Invalidate rect %d (msg: 0x%04x)", hWnd, uMsg);
            // break;
        }

        // Indicate that we've handled erase
        g_filledWindows[hWnd] = true;
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
    // Avoid double-install
    if (g_filledWindows.find(hWnd) != g_filledWindows.end())
        return;

    // Only top-level unrendered windows
    LONG_PTR style   = GetWindowLongPtrW(hWnd, GWL_STYLE);
    // LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) || (style & WS_VISIBLE) /*|| (exStyle & WS_EX_LAYERED)*/)
        return;

    if (SetWindowSubclass(hWnd, FillWindow, SUBCLASS_ID, 0)) {
        g_filledWindows[hWnd] = false;
        Wh_Log(L"Set for %d (%d total)", hWnd, g_filledWindows.size());
    }
}

static void RemoveWindowFiller(HWND hWnd, UINT_PTR uIdSubclass) {
    // Prevent double-cleaning in the future
    auto it = g_filledWindows.find(hWnd);
    if (it == g_filledWindows.end()) {
        return;
    } else {
        g_filledWindows.erase(it);
    }

    RemoveWindowSubclass(hWnd, FillWindow, uIdSubclass);
    // Wh_Log(L"Remove for %d", hWnd);
}

// Hook default windows
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_NCCREATE || Msg == WM_CREATE)
        SetWindowFiller(hWnd);

    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);   
}

// Hook dialog boxes
LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // if (Msg == WM_NCCREATE || Msg == WM_CREATE)
    if (Msg == WM_INITDIALOG)
        SetWindowFiller(hWnd);
    return DefDlgProcW_Original(hWnd, Msg, wParam, lParam);  
}


// Initialize the mod
BOOL Wh_ModInit() {
    using WindhawkUtils::SetFunctionHook;

    // if (!SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
    //     Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit() {
    if (BRUSH)
        DeleteObject(BRUSH);

    while (!g_filledWindows.empty()) {
        auto it = g_filledWindows.begin();
        HWND hWnd = it->first;
        
        // Wh_Log(L"Uninit %d", hWnd);
        RemoveWindowSubclass(hWnd, FillWindow, SUBCLASS_ID);
        g_filledWindows.erase(it);
    }
}