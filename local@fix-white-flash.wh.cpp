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

struct TimerData {
    HWND hWnd;
    UINT Msg;
    WPARAM wParam;
    LPARAM lParam;
};

std::unordered_map<UINT_PTR, TimerData> g_filledWindows;
UINT_PTR g_timerCounter = 1;

const HBRUSH g_windowBrush = CreateSolidBrush(0x191919);


// Hook default windows
decltype(&DefWindowProcW) DefWindowProcW_Original;

VOID CALLBACK DelayWindowProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{    
    // Restore window background
    auto it = g_filledWindows.find(idEvent);
    if (it != g_filledWindows.end())
    {       
        TimerData data = it->second;
        g_filledWindows.erase(it);
        KillTimer(hWnd, idEvent);
        
        Wh_Log(L"Restore %d (msg: %x)", data.hWnd, data.Msg);
        DefWindowProcW_Original(data.hWnd, data.Msg, data.wParam, data.lParam);      
        return;
    }
    // Wh_Log(L"Timer is not killed!");
    // throw std::runtime_error("Unable to restore window rendering (timer is not killed");
}

#define WindowProc DefWindowProcW_Original(hWnd, Msg, wParam, lParam);

LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{       
    // Fill the window with non-white rectangle.
    // Other messages should be ignored, otherwise filling won't work.
    // if (Msg == WM_DWMNCRENDERINGCHANGED && !IsWindowVisible(hWnd))
    if (Msg != WM_NCCALCSIZE)   
        return WindowProc;
    
    // Skip childs and rendered windows
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if ((style & WS_CHILD) || (style & WS_VISIBLE))
        return WindowProc;

    RECT rect;
    if (!GetClientRect(hWnd, &rect))
        return WindowProc;
    
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 1 || height <= 1) 
        return WindowProc;   
    
    Wh_Log(L"Fill %d (msg: %x)", hWnd, Msg);
    FillRect((HDC)wParam, &rect, g_windowBrush);

    // Remove rectangle later
    UINT_PTR timerId = ++g_timerCounter;
    g_filledWindows[timerId] = {hWnd, Msg, wParam, lParam};           
    SetTimer(hWnd, timerId, 100, DelayWindowProc);

    return true;
}  


// Hook dialog boxes
decltype(&DefDlgProcW) DefDlgProcW_Original;

VOID CALLBACK DelayDialogProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{    
    // Restore window background
    auto it = g_filledWindows.find(idEvent);
    if (it != g_filledWindows.end())
    {       
        TimerData data = it->second;
        g_filledWindows.erase(it);
        KillTimer(hWnd, idEvent);
        
        Wh_Log(L"Restore %d (msg: %x)", data.hWnd, data.Msg);
        DefDlgProcW_Original(data.hWnd, data.Msg, data.wParam, data.lParam);      
        return;
    }
    // Wh_Log(L"Timer is not killed!");
    // throw std::runtime_error("Unable to restore window rendering (timer is not killed");
}

#define DialogProc DefDlgProcW_Original(hWnd, Msg, wParam, lParam);

LRESULT WINAPI DefDlgProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    // Fill the dialog with non-white rectangle.
    if (Msg != WM_NCCALCSIZE)   
        return DialogProc;
    
    // Skip dialog controls and rendered windows
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if ((style & WS_CHILD) || (style & WS_VISIBLE))
        return DialogProc;

    RECT rect;
    if (!GetClientRect(hWnd, &rect))
        return DialogProc;
    
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 1 || height <= 1) 
        return DialogProc;   
    
    Wh_Log(L"Fill %d (msg: %x)", hWnd, Msg);
    FillRect((HDC)wParam, &rect, g_windowBrush);

    // Remove rectangle later
    UINT_PTR timerId = ++g_timerCounter;
    g_filledWindows[timerId] = {hWnd, Msg, wParam, lParam};           
    SetTimer(hWnd, timerId, 10, DelayDialogProc);

    return true;
}


BOOL Wh_ModInit()
{
    if (!WindhawkUtils::SetFunctionHook(DefWindowProcW, DefWindowProcW_Hook, &DefWindowProcW_Original))
        Wh_Log(L"Failed to hook DefWindowProcW!");
    if (!WindhawkUtils::SetFunctionHook(DefDlgProcW, DefDlgProcW_Hook, &DefDlgProcW_Original))
        Wh_Log(L"Failed to hook DefDlgProcW!");

    return true;
}

void Wh_ModUninit()
{
    if (g_windowBrush)
        DeleteObject(g_windowBrush);
}