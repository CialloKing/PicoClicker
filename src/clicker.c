#include <windows.h>

#include "resource.h"

static HANDLE timer;
static INPUT clickPair[2];
static UINT runId;
static BOOL running;

static struct
{
    UINT key;
    UINT modifiers;
    int id;
} hotkey;

static struct
{
    UINT key;
    UINT modifiers;
    BOOL active;
    BOOL armed;
} capture;

static void ShowRunState(HWND dialog, PCWSTR status)
{
    // These IDs are contiguous so click settings can be locked as a group.
    for (int id = IDC_INTERVAL; id <= IDC_RIGHT; ++id)
    {
        EnableWindow(GetDlgItem(dialog, id), !running && !capture.active);
    }
    EnableWindow(GetDlgItem(dialog, IDC_HOTKEY), !running);
    EnableWindow(GetDlgItem(dialog, IDC_TOGGLE), hotkey.id && !capture.active);
    SetDlgItemTextW(dialog, IDC_TOGGLE, running ? L"停止" : L"开始");
    SetDlgItemTextW(dialog, IDC_STATUS, status);
}

static BOOL SetHotkey(HWND dialog, UINT key, UINT modifiers)
{
    if (key == hotkey.key && modifiers == hotkey.modifiers)
    {
        return TRUE;
    }

    // The spare ID lets registration fail without releasing the working hotkey.
    int id = hotkey.id == ID_HOTKEY ? ID_HOTKEY_ALTERNATE : ID_HOTKEY;
    if (!RegisterHotKey(dialog, id, modifiers | MOD_NOREPEAT, key))
    {
        return FALSE;
    }
    if (hotkey.id && !UnregisterHotKey(dialog, hotkey.id))
    {
        UnregisterHotKey(dialog, id);
        return FALSE;
    }

    hotkey.key = key;
    hotkey.modifiers = modifiers;
    hotkey.id = id;
    return TRUE;
}

static void ShowHotkey(HWND dialog)
{
    if (!hotkey.id)
    {
        SetDlgItemTextW(dialog, IDC_HOTKEY, L"未设置");
        return;
    }

    WCHAR text[48];
    WCHAR *name = text;
    if (hotkey.modifiers & MOD_CONTROL)
    {
        lstrcpynW(name, L"Ctrl+", 6);
        name += 5;
    }
    if (hotkey.modifiers & MOD_ALT)
    {
        lstrcpynW(name, L"Alt+", 5);
        name += 4;
    }
    if (hotkey.modifiers & MOD_SHIFT)
    {
        lstrcpynW(name, L"Shift+", 7);
        name += 6;
    }

    UINT scan = MapVirtualKeyW(hotkey.key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = (LONG)((scan & 0xffu) << 16);
    if ((scan >> 8) == 0xe0u)
    {
        keyData |= 1L << 24;
    }
    if (!GetKeyNameTextW(keyData, name, (int)(ARRAYSIZE(text) - (name - text))))
    {
        static const WCHAR hex[] = L"0123456789ABCDEF";
        name[0] = L'0';
        name[1] = L'x';
        name[2] = hex[(hotkey.key >> 4) & 15];
        name[3] = hex[hotkey.key & 15];
        name[4] = L'\0';
    }
    SetDlgItemTextW(dialog, IDC_HOTKEY, text);
}

static BOOL IsMainKey(UINT key)
{
    return key >= VK_BACK && key <= 0xfe &&
        (key < VK_SHIFT || key > VK_MENU) && (key < VK_LWIN || key > VK_RWIN) &&
        (key < VK_LSHIFT || key > VK_RMENU) && key != VK_PROCESSKEY && key != VK_PACKET;
}

static UINT PressedMainKey(void)
{
    for (UINT key = VK_BACK; key <= 0xfe; ++key)
    {
        if (IsMainKey(key) && GetAsyncKeyState((int)key) < 0)
        {
            return key;
        }
    }
    return 0;
}

static UINT ReadModifiers(SHORT (WINAPI *readKey)(int))
{
    UINT flags = 0;
    if (readKey(VK_CONTROL) < 0)
    {
        flags |= MOD_CONTROL;
    }
    if (readKey(VK_MENU) < 0)
    {
        flags |= MOD_ALT;
    }
    if (readKey(VK_SHIFT) < 0)
    {
        flags |= MOD_SHIFT;
    }
    if (readKey(VK_LWIN) < 0 || readKey(VK_RWIN) < 0)
    {
        flags |= MOD_WIN;
    }
    return flags;
}

static void FinishCapture(HWND dialog, UINT key)
{
    KillTimer(dialog, ID_CAPTURE_TIMER);
    capture.active = FALSE;

    PCWSTR status = hotkey.id ? L"就绪" : L"请选择可用的快捷键。";
    if (key && key != VK_ESCAPE)
    {
        if (key == VK_F12 || (capture.modifiers & MOD_WIN))
        {
            status = L"这个快捷键由系统保留。";
        }
        else
        {
            status = SetHotkey(dialog, key, capture.modifiers) ?
                L"快捷键已设置。" : L"快捷键不可用，已保留原设置。";
        }
    }
    ShowHotkey(dialog);
    ShowRunState(dialog, status);
}

static void ToggleCapture(HWND dialog)
{
    if (running)
    {
        return;
    }
    if (capture.active)
    {
        FinishCapture(dialog, 0);
        return;
    }
    if (!SetTimer(dialog, ID_CAPTURE_TIMER, USER_TIMER_MINIMUM, NULL))
    {
        SetDlgItemTextW(dialog, IDC_STATUS, L"无法录入快捷键。");
        return;
    }

    // Wait out the key that activated the button when it was opened via the keyboard.
    capture.armed = PressedMainKey() == 0;
    capture.key = 0;
    capture.active = TRUE;
    SetDlgItemTextW(dialog, IDC_HOTKEY, L"按下快捷键…");
    ShowRunState(dialog, L"按下新快捷键，Esc 取消。");
}

static void PollCapture(HWND dialog)
{
    if (GetForegroundWindow() != dialog)
    {
        FinishCapture(dialog, 0);
        return;
    }
    if (capture.key)
    {
        if (GetAsyncKeyState((int)capture.key) >= 0)
        {
            FinishCapture(dialog, capture.key);
        }
        return;
    }

    // Another app's registered chord might bypass this dialog's queued key messages.
    UINT key = PressedMainKey();
    if (!capture.armed)
    {
        capture.armed = key == 0;
    }
    else if (key)
    {
        capture.key = key;
        capture.modifiers = ReadModifiers(GetAsyncKeyState);
    }
}

static BOOL HandleCaptureKey(HWND dialog, const MSG *message)
{
    if (!capture.active || message->message < WM_KEYFIRST || message->message > WM_KEYLAST)
    {
        return FALSE;
    }

    UINT key = (UINT)message->wParam;
    if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN) &&
        capture.armed && !capture.key && IsMainKey(key))
    {
        capture.key = key;
        capture.modifiers = ReadModifiers(GetKeyState);
    }
    else if ((message->message == WM_KEYUP || message->message == WM_SYSKEYUP) &&
        key == capture.key)
    {
        // Wait for release so the captured press can't immediately start clicking.
        FinishCapture(dialog, key);
    }
    return TRUE;
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
    if (!hotkey.id || capture.active)
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
    switch (message)
    {
        case WM_INITDIALOG:
            SetDlgItemInt(dialog, IDC_INTERVAL, 100, FALSE);
            CheckRadioButton(dialog, IDC_LEFT, IDC_RIGHT, IDC_LEFT);
            SetHotkey(dialog, VK_F6, 0);
            ShowHotkey(dialog);
            ShowRunState(dialog, hotkey.id ? L"就绪" : L"F6 不可用，请设置快捷键。");
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_HOTKEY && HIWORD(wParam) == BN_CLICKED)
            {
                ToggleCapture(dialog);
                return TRUE;
            }
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
            if (hotkey.id && wParam == (WPARAM)hotkey.id &&
                lParam == MAKELPARAM(hotkey.modifiers, hotkey.key))
            {
                if (!capture.active)
                {
                    Toggle(dialog);
                }
                else if (capture.armed && !capture.key)
                {
                    capture.key = hotkey.key;
                    capture.modifiers = hotkey.modifiers;
                }
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (capture.active && wParam == ID_CAPTURE_TIMER)
            {
                PollCapture(dialog);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            Stop(dialog, NULL);
            DestroyWindow(dialog);
            return TRUE;
        case WM_DESTROY:
            if (capture.active)
            {
                KillTimer(dialog, ID_CAPTURE_TIMER);
            }
            if (hotkey.id)
            {
                UnregisterHotKey(dialog, hotkey.id);
            }
            PostQuitMessage(0);
            return TRUE;
    }
    return FALSE;
}

static __declspec(noinline) int Run(HINSTANCE instance)
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
            if (!HandleCaptureKey(dialog, &message) && !IsDialogMessageW(dialog, &message))
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

void __cdecl __security_init_cookie(void);

// Initialize the cookie before entering any function that may have a protected stack frame.
__declspec(safebuffers) void WINAPI PicoClickerEntry(void)
{
    __security_init_cookie();
    ExitProcess((UINT)Run(GetModuleHandleW(NULL)));
}
