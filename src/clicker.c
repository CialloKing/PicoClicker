#include <windows.h>
#include <intrin.h>

#include "resource.h"

#ifdef PICO_RESOURCE_PREFIX
// The marker and capacity let the packer identify the unused prefix before writing resources.
#pragma section(".rpack", read)
__declspec(allocate(".rpack")) const DWORD PicoResourcePrefix[PICO_RESOURCE_PREFIX / sizeof(DWORD)] =
{
    0x4f434950u, PICO_RESOURCE_PREFIX
};
#endif

static HANDLE timer;
static INPUT clickPair[2];
static UINT runId;
static BOOL running;

#ifdef PICOCLICKER_LITE
static PCWSTR idleStatus;
#else
typedef struct
{
    BYTE key;
    BYTE modifiers;
} HotkeyBinding;

_Static_assert(sizeof(HotkeyBinding) == 2, "Shortcut file format");
static HotkeyBinding hotkey;
static int hotkeyId;

// Captured main keys start at VK_BACK, leaving the lower values for capture phases.
enum
{
    CAPTURE_IDLE,
    CAPTURE_WAIT_RELEASE,
    CAPTURE_READY
};

static struct
{
    UINT key;
    UINT modifiers;
} capture;
#endif

static void SetControlText(HWND dialog, int id, PCWSTR text)
{
    // The edit, radio buttons and text updates share one control-message import.
    SendDlgItemMessageW(dialog, id, WM_SETTEXT, 0, (LPARAM)text);
}

static void ShowRunState(HWND dialog, PCWSTR status)
{
#ifdef PICOCLICKER_LITE
    const int lastControl = IDC_RIGHT;
#else
    const int lastControl = IDC_HOTKEY;
#endif
    // Share the two Win32 calls across the contiguous control IDs.
    for (int id = IDC_INTERVAL; id <= lastControl; ++id)
    {
        BOOL enabled = !running;
#ifndef PICOCLICKER_LITE
        if (id == IDC_STATUS)
        {
            continue;
        }
        if (id == IDC_TOGGLE)
        {
            enabled = hotkeyId && capture.key == CAPTURE_IDLE;
        }
        else if (id != IDC_HOTKEY && capture.key != CAPTURE_IDLE)
        {
            enabled = FALSE;
        }
#endif
        EnableWindow(GetDlgItem(dialog, id), enabled);
    }
    SetControlText(dialog, IDC_TOGGLE, running ? L"停止" : L"开始");
    SetControlText(dialog, IDC_STATUS, status);
}

#ifndef PICOCLICKER_LITE
static BOOL SetHotkey(HWND dialog, UINT key, UINT modifiers)
{
    if (key == hotkey.key && modifiers == hotkey.modifiers)
    {
        return TRUE;
    }

    // The spare ID lets registration fail without releasing the working hotkey.
    int id = hotkeyId == ID_HOTKEY ? ID_HOTKEY_ALTERNATE : ID_HOTKEY;
    if (!RegisterHotKey(dialog, id, modifiers | MOD_NOREPEAT, key))
    {
        return FALSE;
    }
    if (hotkeyId && !UnregisterHotKey(dialog, hotkeyId))
    {
        UnregisterHotKey(dialog, id);
        return FALSE;
    }

    hotkey.key = (BYTE)key;
    hotkey.modifiers = (BYTE)modifiers;
    hotkeyId = id;
    return TRUE;
}

static WCHAR HexDigit(UINT digit)
{
    return (WCHAR)(digit + (digit < 10 ? L'0' : L'A' - 10));
}

static void ShowHotkey(HWND dialog)
{
    if (!hotkeyId)
    {
        SetControlText(dialog, IDC_HOTKEY, L"未设置");
        return;
    }

    WCHAR text[48];
    WCHAR *name = text;
    // The fixed labels fit in ASCII and share one widening loop.
    static const struct
    {
        BYTE bit;
        char text[7];
    } prefixes[] =
    {
        {
            MOD_CONTROL, "Ctrl+"
        },
        {
            MOD_ALT, "Alt+"
        },
        {
            MOD_SHIFT, "Shift+"
        }
    };
    for (UINT i = 0; i < ARRAYSIZE(prefixes); ++i)
    {
        if (hotkey.modifiers & prefixes[i].bit)
        {
            const char *letter = prefixes[i].text;
            while (*letter)
            {
                *name++ = (WCHAR)*letter++;
            }
        }
    }

    UINT scan = MapVirtualKeyW(hotkey.key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = (LONG)((scan & 0xffu) << 16);
    if ((scan >> 8) == 0xe0u)
    {
        keyData |= 1L << 24;
    }
    if (!GetKeyNameTextW(keyData, name, (int)(ARRAYSIZE(text) - (name - text))))
    {
        name[0] = L'0';
        name[1] = L'x';
        name[2] = HexDigit((hotkey.key >> 4) & 15);
        name[3] = HexDigit(hotkey.key & 15);
        name[4] = L'\0';
    }
    SetControlText(dialog, IDC_HOTKEY, text);
}

static BOOL IsMainKey(UINT key)
{
#ifdef _M_X64
    // BT indexes all 256 virtual keys. A set bit lets polling and queued messages accept the same keys.
#define KEY_BIT(vk) (1u << ((vk) & 31u))
    static const LONG mainKeys[] =
    {
        (LONG)~(0xffu | KEY_BIT(VK_SHIFT) | KEY_BIT(VK_CONTROL) | KEY_BIT(VK_MENU)),
        -1,
        (LONG)~(KEY_BIT(VK_LWIN) | KEY_BIT(VK_RWIN)),
        -1,
        -1,
        (LONG)~(KEY_BIT(VK_LSHIFT) | KEY_BIT(VK_RSHIFT) | KEY_BIT(VK_LCONTROL) |
            KEY_BIT(VK_RCONTROL) | KEY_BIT(VK_LMENU) | KEY_BIT(VK_RMENU)),
        -1,
        (LONG)~(KEY_BIT(VK_PROCESSKEY) | KEY_BIT(VK_PACKET) | KEY_BIT(0xff))
    };
#undef KEY_BIT
    return key < 256 && _bittest(mainKeys, (LONG)key);
#else
    // On x86 the range checks leave room for relocations in the read-only section.
    return key >= VK_BACK && key <= 0xfe &&
        (key < VK_SHIFT || key > VK_MENU) && (key < VK_LWIN || key > VK_RWIN) &&
        (key < VK_LSHIFT || key > VK_RMENU) && key != VK_PROCESSKEY && key != VK_PACKET;
#endif
}

static BOOL IsBindableHotkey(UINT key, UINT modifiers)
{
    return IsMainKey(key) && key != VK_ESCAPE && key != VK_F12 &&
        !(modifiers & ~(MOD_ALT | MOD_CONTROL | MOD_SHIFT));
}

enum
{
    TEMP_SUFFIX_LENGTH = 9
};

// Resolve the process's profile path once for both reading and replacing the record.
static WCHAR hotkeyPath[MAX_PATH];
static WCHAR hotkeyTemporaryPath[MAX_PATH];
static DWORD hotkeyPathLength;

// Keeping path validation out of the dialog callback makes its conditional jumps shorter.
static __declspec(noinline) DWORD GetHotkeyPath(WCHAR *path)
{
    // Reserve space for the temporary suffix and reject unresolved or relative paths.
    const DWORD capacity = MAX_PATH - TEMP_SUFFIX_LENGTH;
    DWORD length = ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\PicoClicker.dat", path, capacity);
    if (length < 4 || length > capacity || (UINT)(path[0] | 32) - L'a' >= 26 ||
        path[1] != L':' || path[2] != L'\\')
    {
        return 0;
    }
    return length - 1;
}

static void RestoreHotkey(HWND dialog)
{
    hotkeyPathLength = GetHotkeyPath(hotkeyPath);
    if (hotkeyPathLength)
    {
        // The profile path is fixed for the process, so later saves only write the suffix.
#if defined(_M_IX86) || defined(_M_X64)
        __movsw(hotkeyTemporaryPath, hotkeyPath, hotkeyPathLength);
#else
        for (DWORD i = 0; i < hotkeyPathLength; ++i)
        {
            hotkeyTemporaryPath[i] = hotkeyPath[i];
        }
#endif
        hotkeyTemporaryPath[hotkeyPathLength] = L'.';
        HANDLE file = CreateFileW(hotkeyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            // Read one extra byte so an overlong file is rejected.
            BYTE record[3];
            DWORD received;
            BOOL read = ReadFile(file, record, sizeof(record), &received, NULL);
            CloseHandle(file);
            if (read && received == 2 && IsBindableHotkey(record[0], record[1]) &&
                SetHotkey(dialog, record[0], record[1]))
            {
                return;
            }
        }
    }

    // A fallback is only for this session; the stored choice may be available next time.
    SetHotkey(dialog, VK_F1, 0);
}

static BOOL SaveHotkey(void)
{
    DWORD length = hotkeyPathLength;
    if (!length)
    {
        return FALSE;
    }

    ++length;
    // The stack-cookie startup already imports this counter.
    LARGE_INTEGER counter;
    DWORD suffix = GetCurrentProcessId();
    if (QueryPerformanceCounter(&counter))
    {
        suffix ^= counter.LowPart;
    }
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        UINT digit = (suffix >> shift) & 15;
        hotkeyTemporaryPath[length++] = HexDigit(digit);
    }
    hotkeyTemporaryPath[length] = L'\0';

    HANDLE file = CreateFileW(hotkeyTemporaryPath, GENERIC_WRITE, 0, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    DWORD written;
    BOOL saved = WriteFile(file, &hotkey, sizeof(hotkey), &written, NULL) && written == sizeof(hotkey);
    BOOL closed = CloseHandle(file);

    // Replace only after a complete write, so a failed save preserves the previous record.
    if (saved && closed && MoveFileExW(hotkeyTemporaryPath, hotkeyPath, MOVEFILE_REPLACE_EXISTING))
    {
        return TRUE;
    }
    DeleteFileW(hotkeyTemporaryPath);
    return FALSE;
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

static UINT ReadModifiers(BOOL asynchronous)
{
    static const BYTE keys[] =
    {
        VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN
    };
    static const BYTE modifiers[] =
    {
        MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN, MOD_WIN
    };
    UINT flags = 0;
    for (UINT index = 0; index < ARRAYSIZE(keys); ++index)
    {
        SHORT state = asynchronous ? GetAsyncKeyState(keys[index]) : GetKeyState(keys[index]);
        if (state < 0)
        {
            flags |= modifiers[index];
        }
    }
    return flags;
}

static void FinishCapture(HWND dialog, UINT key)
{
    KillTimer(dialog, ID_CAPTURE_TIMER);
    capture.key = CAPTURE_IDLE;

    PCWSTR status = hotkeyId ? L"就绪" : L"请设快捷键";
    if (key && key != VK_ESCAPE)
    {
        if (!IsBindableHotkey(key, capture.modifiers))
        {
            status = L"系统保留键";
        }
        else if (SetHotkey(dialog, key, capture.modifiers))
        {
            status = SaveHotkey() ? L"已改键" : L"已改键，保存失败";
        }
        else
        {
            status = L"不可用，保留原设置";
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
    if (capture.key != CAPTURE_IDLE)
    {
        FinishCapture(dialog, 0);
        return;
    }
    if (!SetTimer(dialog, ID_CAPTURE_TIMER, USER_TIMER_MINIMUM, NULL))
    {
        SetControlText(dialog, IDC_STATUS, L"录入失败");
        return;
    }

    // Wait out the key that activated the button when it was opened via the keyboard.
    capture.key = PressedMainKey() ? CAPTURE_WAIT_RELEASE : CAPTURE_READY;
    SetControlText(dialog, IDC_HOTKEY, L"按下快捷键…");
    ShowRunState(dialog, L"按键，Esc 取消");
}

static void PollCapture(HWND dialog)
{
    if (capture.key >= VK_BACK)
    {
        if (GetAsyncKeyState((int)capture.key) >= 0)
        {
            FinishCapture(dialog, capture.key);
        }
        return;
    }

    // Another app's registered chord might bypass this dialog's queued key messages.
    UINT key = PressedMainKey();
    if (capture.key == CAPTURE_WAIT_RELEASE)
    {
        if (!key)
        {
            capture.key = CAPTURE_READY;
        }
    }
    else if (key)
    {
        capture.key = key;
        capture.modifiers = ReadModifiers(TRUE);
    }
}

static BOOL HandleCaptureKey(HWND dialog, const MSG *message)
{
    if (capture.key == CAPTURE_IDLE || message->message < WM_KEYFIRST || message->message > WM_KEYLAST)
    {
        return FALSE;
    }

    UINT key = (UINT)message->wParam;
    UINT kind = message->message & ~(WM_SYSKEYDOWN ^ WM_KEYDOWN);
    if (kind == WM_KEYDOWN && capture.key == CAPTURE_READY && IsMainKey(key))
    {
        capture.key = key;
        capture.modifiers = ReadModifiers(FALSE);
    }
    else if (kind == WM_KEYUP && capture.key >= VK_BACK && key == capture.key)
    {
        // Wait for release so the captured press can't immediately start clicking.
        FinishCapture(dialog, key);
    }
    return TRUE;
}
#endif

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
    UINT length = (UINT)SendDlgItemMessageW(dialog, IDC_INTERVAL, WM_GETTEXT, ARRAYSIZE(text), (LPARAM)text);
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

static BOOL IsClickButtonChecked(HWND dialog, int id)
{
    return SendDlgItemMessageW(dialog, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void SelectClickPair(HWND dialog)
{
    // The zero-initialized pair is mouse input and each release flag follows its press flag.
    _Static_assert(INPUT_MOUSE == 0 && MOUSEEVENTF_LEFTUP == (MOUSEEVENTF_LEFTDOWN << 1) &&
        MOUSEEVENTF_MIDDLEUP == (MOUSEEVENTF_MIDDLEDOWN << 1) &&
        MOUSEEVENTF_RIGHTUP == (MOUSEEVENTF_RIGHTDOWN << 1), "Mouse button flags");

    DWORD down = MOUSEEVENTF_LEFTDOWN;
    if (IsClickButtonChecked(dialog, IDC_MIDDLE))
    {
        down = MOUSEEVENTF_MIDDLEDOWN;
    }
    else if (IsClickButtonChecked(dialog, IDC_RIGHT))
    {
        down = MOUSEEVENTF_RIGHTDOWN;
    }

    clickPair[0].mi.dwFlags = down;
    clickPair[1].mi.dwFlags = down << 1;
}

static void Toggle(HWND dialog)
{
    if (running)
    {
#ifdef PICOCLICKER_LITE
        Stop(dialog, idleStatus);
#else
        Stop(dialog, L"就绪");
#endif
        return;
    }
#ifndef PICOCLICKER_LITE
    if (!hotkeyId || capture.key != CAPTURE_IDLE)
    {
        return;
    }
#endif

    UINT interval = ReadInterval(dialog);
    if (interval == 0 || interval > 60000)
    {
        SetControlText(dialog, IDC_STATUS, L"间隔限1–60000ms");
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
        SetControlText(dialog, IDC_STATUS, L"计时器启动失败");
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
        Stop(dialog, L"点击失败");
    }
}

static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            SendDlgItemMessageW(dialog, IDC_LEFT, BM_SETCHECK, BST_CHECKED, 0);
#ifdef PICOCLICKER_LITE
            // Manual start and stop are still available when another app owns F1.
            idleStatus = RegisterHotKey(dialog, ID_HOTKEY, MOD_NOREPEAT, VK_F1) ? L"F1 启停" : L"F1 不可用";
            SetControlText(dialog, IDC_STATUS, idleStatus);
#else
            RestoreHotkey(dialog);
            ShowHotkey(dialog);
            ShowRunState(dialog, hotkeyId ? L"就绪" : L"F1 不可用，请改键");
#endif
            return TRUE;
        case WM_COMMAND:
#ifndef PICOCLICKER_LITE
            if ((DWORD)wParam == MAKEWPARAM(IDC_HOTKEY, BN_CLICKED))
            {
                ToggleCapture(dialog);
                return TRUE;
            }
#endif
            if ((DWORD)wParam == MAKEWPARAM(IDC_TOGGLE, BN_CLICKED))
            {
                Toggle(dialog);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                goto close;
            }
            break;
        case WM_HOTKEY:
#ifdef PICOCLICKER_LITE
            if (wParam == ID_HOTKEY && lParam == MAKELPARAM(0, VK_F1))
            {
                Toggle(dialog);
                return TRUE;
            }
            break;
#else
            if (hotkeyId && wParam == (WPARAM)hotkeyId &&
                lParam == MAKELPARAM(hotkey.modifiers, hotkey.key))
            {
                if (capture.key == CAPTURE_IDLE)
                {
                    Toggle(dialog);
                }
                else if (capture.key == CAPTURE_READY)
                {
                    capture.key = hotkey.key;
                    capture.modifiers = hotkey.modifiers;
                }
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (capture.key != CAPTURE_IDLE && wParam == ID_CAPTURE_TIMER)
            {
                PollCapture(dialog);
                return TRUE;
            }
            break;
        case WM_ACTIVATE:
            // Stop polling as soon as the dialog loses activation during capture.
            if (LOWORD(wParam) == WA_INACTIVE && capture.key != CAPTURE_IDLE)
            {
                FinishCapture(dialog, 0);
            }
            break;
#endif
        case WM_CLOSE:
        close:
            Stop(dialog, NULL);
            DestroyWindow(dialog);
            return TRUE;
        case WM_DESTROY:
#ifdef PICOCLICKER_LITE
            UnregisterHotKey(dialog, ID_HOTKEY);
#else
            if (capture.key != CAPTURE_IDLE)
            {
                KillTimer(dialog, ID_CAPTURE_TIMER);
            }
            if (hotkeyId)
            {
                UnregisterHotKey(dialog, hotkeyId);
            }
#endif
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
        MessageBoxW(NULL, L"窗口创建失败", L"PicoClicker", MB_ICONERROR);
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
            MessageBoxW(dialog, L"计时失败", L"PicoClicker", MB_ICONERROR);
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
#ifdef PICOCLICKER_LITE
            if (!IsDialogMessageW(dialog, &message))
#else
            if (!HandleCaptureKey(dialog, &message) && !IsDialogMessageW(dialog, &message))
#endif
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
    // ExitProcess closes the timer handle immediately after Run returns.
    return result;
}

#if defined(_M_IX86) || defined(_M_X64)
// Zero-initialized storage keeps these CRT globals out of the file's raw data.
__declspec(align(64)) UINT_PTR __security_cookie;
UINT_PTR __security_cookie_complement;
#endif

void __cdecl __security_init_cookie(void);

// The linker already supplies the module base needed to load the dialog resource.
extern IMAGE_DOS_HEADER __ImageBase;

// Initialize the cookie before entering any function that may have a protected stack frame.
__declspec(safebuffers) void WINAPI PicoClickerEntry(void)
{
#if defined(_M_IX86) || defined(_M_X64)
#if defined(_M_IX86)
    const UINT_PTR initialCookie = 0xBB40E64Eu;
#else
    const UINT_PTR initialCookie = 0x00002B992DDFA232ull;
#endif

    // Use the CRT's initial value only if the loader has left the cookie unset.
    if (!__security_cookie)
    {
        __security_cookie = initialCookie;
    }
#endif
    __security_init_cookie();
#if defined(_M_IX86) || defined(_M_X64)
    if (!__security_cookie || __security_cookie == initialCookie)
    {
        __fastfail(FAST_FAIL_GS_COOKIE_INIT);
    }
#endif
    // Set awareness before creating the dialog so the embedded manifest only needs UAC.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ExitProcess((UINT)Run((HINSTANCE)&__ImageBase));
}
