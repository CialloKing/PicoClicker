#include <windows.h>
#include <intrin.h>
#include <stddef.h>

#include "compact_cookie.h"
#include "resource.h"

#ifdef PICO_RESOURCE_PREFIX
// The marker and capacity let the packer identify the unused prefix before writing resources.
#pragma section(".rpack", read)
__declspec(allocate(".rpack")) const DWORD PicoResourcePrefix[PICO_RESOURCE_PREFIX / sizeof(DWORD)] =
{
    0x4f434950u, PICO_RESOURCE_PREFIX
};
#endif

#ifdef PICOCLICKER_ASM_RUN
// The assembly keeps one base for the writable state and one for the IAT.
struct PicoRunData
{
    UINT generation;
#ifdef _M_X64
    BYTE selectedButton;
    BYTE closing;
#endif
    HANDLE timer;
    LPCSTR idleStatus;
    INPUT clickPair[2];
#ifdef _M_IX86
    BYTE selectedButton;
    BYTE closing;
#endif
} picoRunData;
#ifdef _M_IX86
_Static_assert(offsetof(struct PicoRunData, timer) == 4 &&
    offsetof(struct PicoRunData, idleStatus) == 8 &&
    offsetof(struct PicoRunData, clickPair) == 12 &&
    offsetof(struct PicoRunData, selectedButton) == 68 &&
    offsetof(struct PicoRunData, closing) == 69 &&
    offsetof(INPUT, mi.dwFlags) == 16 && INPUT_MOUSE == 0, "x86 dialog state layout");
#else
_Static_assert(offsetof(struct PicoRunData, selectedButton) == 4 &&
    offsetof(struct PicoRunData, closing) == 5 &&
    offsetof(struct PicoRunData, timer) == 8 &&
    offsetof(struct PicoRunData, idleStatus) == 16 &&
    offsetof(struct PicoRunData, clickPair) == 24 &&
    offsetof(INPUT, mi.dwFlags) == 20 && INPUT_MOUSE == 0, "x64 dialog state layout");
#endif
#else
#define PicoTimerError PICO_TEXT("计时器启动失败", "Timer Err")

static HANDLE timer;
static INPUT clickPair[2];
// Each transition advances the generation. Odd generations are active.
static UINT runState;

#ifdef PICOCLICKER_LITE
static LPCTSTR idleStatus;
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

static void SetControlText(HWND dialog, int id, LPCTSTR text)
{
    // The edit, radio buttons and text updates share one control-message import.
    SendDlgItemMessage(dialog, id, WM_SETTEXT, 0, (LPARAM)text);
}

static void ShowRunState(HWND dialog, LPCTSTR status)
{
#ifdef PICOCLICKER_LITE
    const int lastControl = IDC_RIGHT;
#else
    const int lastControl = IDC_HOTKEY;
#endif
    // Share the two Win32 calls across the contiguous control IDs.
    for (int id = IDC_INTERVAL; id <= lastControl; ++id)
    {
        BOOL enabled = !(runState & 1);
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
    SetControlText(dialog, IDC_TOGGLE, (runState & 1) ? PICO_TEXT("停止", "Stop") : PICO_TEXT("开始", "Start"));
    SetControlText(dialog, IDC_STATUS, status);
}
#endif

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
    if (runState & 1)
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

#ifndef PICOCLICKER_ASM_RUN
static void Stop(HWND dialog, LPCTSTR status)
{
    if (runState & 1)
    {
        ++runState;
        CancelWaitableTimer(timer);
    }
    if (status)
    {
        ShowRunState(dialog, status);
    }
}

static UINT ReadInterval(HWND dialog)
{
    TCHAR text[7];
    UINT length = (UINT)SendDlgItemMessage(dialog, IDC_INTERVAL, WM_GETTEXT, ARRAYSIZE(text), (LPARAM)text);
    UINT interval = 0;

    // Read one extra character so overlong pasted input cannot be accepted after truncation.
    if (length >= ARRAYSIZE(text) - 1)
    {
        return 0;
    }
    for (UINT index = 0; index < length; ++index)
    {
        UINT digit = (UINT)text[index] - '0';
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
    // The zero-initialized pair is mouse input and each release flag follows its press flag.
    _Static_assert(INPUT_MOUSE == 0 && MOUSEEVENTF_LEFTUP == (MOUSEEVENTF_LEFTDOWN << 1) &&
        MOUSEEVENTF_MIDDLEUP == (MOUSEEVENTF_MIDDLEDOWN << 1) &&
        MOUSEEVENTF_RIGHTUP == (MOUSEEVENTF_RIGHTDOWN << 1), "Mouse button flags");
    _Static_assert(IDC_RIGHT == IDC_MIDDLE + 1, "Mouse button control IDs");

    DWORD down = MOUSEEVENTF_LEFTDOWN;
    for (int id = IDC_MIDDLE; id <= IDC_RIGHT; ++id)
    {
        if (SendDlgItemMessage(dialog, id, BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            down = id == IDC_MIDDLE ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_RIGHTDOWN;
            break;
        }
    }

    clickPair[0].mi.dwFlags = down;
    clickPair[1].mi.dwFlags = down << 1;
}

static void Toggle(HWND dialog)
{
    if (runState & 1)
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
        SetControlText(dialog, IDC_STATUS, PICO_TEXT("间隔限1–60000ms", "1-60000ms"));
        return;
    }

    if (!timer)
    {
        timer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
    }

    // Negative due times are relative. At 60,000 ms the conversion still fits in LONG.
    LARGE_INTEGER due;
    due.QuadPart = -(LONG)(interval * 10000u);
    if (!timer || !SetWaitableTimer(timer, &due, (LONG)interval, NULL, NULL, FALSE))
    {
        SetControlText(dialog, IDC_STATUS, PicoTimerError);
        return;
    }

    SelectClickPair(dialog);
    ++runState;
    ShowRunState(dialog, PICO_TEXT("连点中", "On"));
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
        Stop(dialog, PICO_TEXT("点击失败", "Click Err"));
    }
}

static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            SendDlgItemMessage(dialog, IDC_LEFT, BM_SETCHECK, BST_CHECKED, 0);
#ifdef PICOCLICKER_LITE
            // Manual start and stop are still available when another app owns F1.
            idleStatus = RegisterHotKey(dialog, ID_HOTKEY, MOD_NOREPEAT, VK_F1)
                ? PICO_TEXT("F1 启停", "F1") : PICO_TEXT("F1 不可用", "F1 Busy");
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
                goto toggle;
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
                goto toggle;
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
        toggle:
            Toggle(dialog);
            return TRUE;
        case WM_CLOSE:
        close:
            Stop(dialog, NULL);
            DestroyWindow(dialog);
            return TRUE;
        case WM_DESTROY:
#ifdef PICOCLICKER_LITE
#ifdef PICOCLICKER_ASCII
            // The experimental dialog owns F1 for the process lifetime.
#else
            UnregisterHotKey(dialog, ID_HOTKEY);
#endif
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

#define ShowRunError(dialog, text) MessageBox((dialog), (text), L"PicoClicker", MB_ICONERROR)
#endif

#if defined(PICOCLICKER_ASM_RUN) && defined(_M_IX86)
// The assembly keeps MSG and POINT in one slot and writes one INPUT pair.
_Static_assert(sizeof(MSG) == 28 && offsetof(MSG, message) == 4 && offsetof(MSG, wParam) == 8,
    "x86 message layout");
_Static_assert(sizeof(POINT) == 8 && offsetof(POINT, y) == 4 && sizeof(INPUT) == 28,
    "x86 click layout");
#elif defined(PICOCLICKER_ASM_RUN)
_Static_assert(sizeof(MSG) == 48 && offsetof(MSG, message) == 8 && offsetof(MSG, wParam) == 16,
    "x64 message layout");
_Static_assert(sizeof(POINT) == 8 && sizeof(INPUT) == 40, "x64 click layout");
#else
static __declspec(noinline) int Run(HINSTANCE instance)
{
    HWND dialog = CreateDialogParam(instance, MAKEINTRESOURCE(IDD_MAIN), NULL, DialogProc, 0);
    if (!dialog)
    {
        ShowRunError(NULL, PICO_TEXT("窗口创建失败", "UI Err"));
        return 1;
    }

    int result = 1;
    for (;;)
    {
        UINT waitingRun = runState;
        DWORD count = waitingRun & 1;
        DWORD ready = MsgWaitForMultipleObjectsEx(count, &timer, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (ready == WAIT_FAILED)
        {
            Stop(dialog, NULL);
            ShowRunError(dialog, PICO_TEXT("计时失败", "Timer Err"));
            DestroyWindow(dialog);
            break;
        }

        // Give queued stop commands a chance to run, even when the timer is already ready.
        MSG message;
        for (unsigned remaining = 32;
             remaining && PeekMessage(&message, NULL, 0, 0, PM_REMOVE);
             --remaining)
        {
            if (message.message == WM_QUIT)
            {
                result = (int)message.wParam;
                goto done;
            }
#ifdef PICOCLICKER_LITE
            if (!IsDialogMessage(dialog, &message))
#else
            if (!HandleCaptureKey(dialog, &message) && !IsDialogMessage(dialog, &message))
#endif
            {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
        }
        // The old signal cannot belong to a run restarted while the queue was dispatched.
        if (count && ready == WAIT_OBJECT_0 && runState == waitingRun)
        {
            Click(dialog);
        }
    }

done:
    // ExitProcess closes the timer handle immediately after Run returns.
    return result;
}
#endif

#ifdef INITIAL_COOKIE
#ifdef PICOCLICKER_LOADER_COOKIE
// The loader replaces the on-disk sentinel. Keeping it with the read-only data
// lets the entire writable section remain zero-initialized on disk.
// The assembly build places this symbol immediately before its IAT.
#ifndef PICOCLICKER_ASM_RUN
#pragma section(".rdata$pcookie", read)
__declspec(allocate(".rdata$pcookie")) UINT_PTR __security_cookie = INITIAL_COOKIE;
#endif
#else
// Zero-initialized storage keeps these CRT globals out of the file's raw data.
__declspec(align(64)) UINT_PTR __security_cookie;
#endif
UINT_PTR __security_cookie_complement;
#endif

#ifndef PICOCLICKER_ASM_RUN
void __cdecl __security_init_cookie(void);

// The linker already supplies the module base needed to load the dialog resource.
extern IMAGE_DOS_HEADER __ImageBase;

// Initialize the cookie before entering any function that may have a protected stack frame.
__declspec(safebuffers) void WINAPI PicoClickerEntry(void)
{
#ifdef PICOCLICKER_LOADER_COOKIE
    // Read the value the loader wrote, even though this code never stores to the slot.
    UINT_PTR cookie = *(volatile UINT_PTR *)&__security_cookie;
    if (!cookie || cookie == INITIAL_COOKIE)
    {
        __fastfail(FAST_FAIL_GS_COOKIE_INIT);
    }
    __security_cookie_complement = ~cookie;
#else
#ifdef INITIAL_COOKIE
    // Use the CRT's initial value only if the loader has left the cookie unset.
    if (!__security_cookie)
    {
        __security_cookie = INITIAL_COOKIE;
    }
#endif
    __security_init_cookie();
#ifdef INITIAL_COOKIE
    if (!__security_cookie || __security_cookie == INITIAL_COOKIE)
    {
        __fastfail(FAST_FAIL_GS_COOKIE_INIT);
    }
#endif
#endif
    // Set awareness before creating the dialog so the embedded manifest only needs UAC.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ExitProcess((UINT)Run((HINSTANCE)&__ImageBase));
}
#endif
