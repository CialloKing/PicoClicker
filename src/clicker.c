#include <windows.h>

#include "resource.h"

static HANDLE timer;
static INPUT clickPair[2];
static UINT runId;
static BOOL hotkeyReady;
static BOOL running;

static void ShowRunState(HWND dialog, PCWSTR status)
{
    // These control IDs are contiguous so settings can be locked while a run uses them.
    for (int id = IDC_INTERVAL; id <= IDC_RIGHT; ++id)
    {
        EnableWindow(GetDlgItem(dialog, id), !running);
    }
    SetDlgItemTextW(dialog, IDC_TOGGLE, running ? L"停止 (F6)" : L"开始 (F6)");
    SetDlgItemTextW(dialog, IDC_STATUS, status);
}

static void Stop(HWND dialog, PCWSTR status)
{
    if (running)
    {
        running = FALSE;
        CancelWaitableTimer(timer);
    }
    if (status)
    {
        ShowRunState(dialog, status);
    }
}

static UINT ReadInterval(HWND dialog)
{
    WCHAR text[7];
    UINT length = GetDlgItemTextW(dialog, IDC_INTERVAL, text, ARRAYSIZE(text));
    UINT interval = 0;

    // Read one extra character so overlong pasted input cannot be accepted after truncation.
    if (length >= ARRAYSIZE(text) - 1)
    {
        return 0;
    }
    for (UINT index = 0; index < length; ++index)
    {
        UINT digit = (UINT)text[index] - L'0';
        if (digit > 9)
        {
            return 0;
        }
        interval = interval * 10 + digit;
    }
    return interval;
}

static void SelectClickPair(HWND dialog)
{
    static const DWORD flags[][2] =
    {
        {
            MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP
        },
        {
            MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP
        },
        {
            MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP
        }
    };
    UINT button = 0;

    if (IsDlgButtonChecked(dialog, IDC_MIDDLE) == BST_CHECKED)
    {
        button = 1;
    }
    else if (IsDlgButtonChecked(dialog, IDC_RIGHT) == BST_CHECKED)
    {
        button = 2;
    }

    clickPair[0].type = INPUT_MOUSE;
    clickPair[0].mi.dwFlags = flags[button][0];
    clickPair[1].type = INPUT_MOUSE;
    clickPair[1].mi.dwFlags = flags[button][1];
}

static void Toggle(HWND dialog)
{
    if (running)
    {
        Stop(dialog, L"就绪");
        return;
    }
    if (!hotkeyReady)
    {
        return;
    }

    UINT interval = ReadInterval(dialog);
    if (interval == 0 || interval > 60000)
    {
        SetDlgItemTextW(dialog, IDC_STATUS, L"请输入 1–60,000 ms 的整数。");
        return;
    }

    if (!timer)
    {
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
    }

    // Negative due times are relative. At 60,000 ms the conversion still fits in LONG.
    LARGE_INTEGER due;
    due.QuadPart = -(LONG)(interval * 10000u);
    if (!timer || !SetWaitableTimer(timer, &due, (LONG)interval, NULL, NULL, FALSE))
    {
        SetDlgItemTextW(dialog, IDC_STATUS, L"无法启动高精度计时器。");
        return;
    }

    SelectClickPair(dialog);
    ++runId;
    running = TRUE;
    ShowRunState(dialog, L"连点中");
}

static void Click(HWND dialog)
{
    // A click started with the mouse must not immediately click the app's own button.
    POINT cursor;
    if (!GetCursorPos(&cursor) || GetAncestor(WindowFromPoint(cursor), GA_ROOT) == dialog)
    {
        return;
    }

    UINT inserted = SendInput(ARRAYSIZE(clickPair), clickPair, sizeof(clickPair[0]));
    if (inserted != ARRAYSIZE(clickPair))
    {
        if (inserted == 1)
        {
            // Try to release the button when only the down event made it into the stream.
            SendInput(1, &clickPair[1], sizeof(clickPair[1]));
        }
        Stop(dialog, L"无法发送点击，已停止。");
    }
}

static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (message)
    {
        case WM_INITDIALOG:
            SetDlgItemInt(dialog, IDC_INTERVAL, 100, FALSE);
            CheckRadioButton(dialog, IDC_LEFT, IDC_RIGHT, IDC_LEFT);
            hotkeyReady = RegisterHotKey(dialog, ID_HOTKEY, MOD_NOREPEAT, VK_F6);
            if (!hotkeyReady)
            {
                SetDlgItemTextW(dialog, IDC_STATUS, L"无法注册 F6，已禁用启动。");
                EnableWindow(GetDlgItem(dialog, IDC_TOGGLE), FALSE);
            }
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_TOGGLE && HIWORD(wParam) == BN_CLICKED)
            {
                Toggle(dialog);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                return TRUE;
            }
            break;
        case WM_HOTKEY:
            if (wParam == ID_HOTKEY)
            {
                Toggle(dialog);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            Stop(dialog, NULL);
            DestroyWindow(dialog);
            return TRUE;
        case WM_DESTROY:
            if (hotkeyReady)
            {
                UnregisterHotKey(dialog, ID_HOTKEY);
            }
            PostQuitMessage(0);
            return TRUE;
    }
    return FALSE;
}

static int Run(HINSTANCE instance)
{
    HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), NULL, DialogProc, 0);
    if (!dialog)
    {
        MessageBoxW(NULL, L"无法创建窗口。", L"PicoClicker", MB_ICONERROR);
        return 1;
    }

    int result = 1;
    for (;;)
    {
        UINT waitingRun = runId;
        DWORD count = running ? 1 : 0;
        DWORD ready = MsgWaitForMultipleObjectsEx(count, &timer, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (ready == WAIT_FAILED)
        {
            Stop(dialog, NULL);
            MessageBoxW(dialog, L"计时等待失败，已停止。", L"PicoClicker", MB_ICONERROR);
            DestroyWindow(dialog);
            break;
        }

        // Give queued stop commands a chance to run, even when the timer is already ready.
        MSG message;
        for (unsigned remaining = 32;
             remaining && PeekMessageW(&message, NULL, 0, 0, PM_REMOVE);
             --remaining)
        {
            if (message.message == WM_QUIT)
            {
                result = (int)message.wParam;
                goto done;
            }
            if (!IsDialogMessageW(dialog, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        // The old signal cannot belong to a run restarted while the queue was dispatched.
        if (count && ready == WAIT_OBJECT_0 && running && runId == waitingRun)
        {
            Click(dialog);
        }
    }

done:
    if (timer)
    {
        CloseHandle(timer);
    }
    return result;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    (void)previous;
    (void)arguments;
    (void)show;
    return Run(instance);
}
