#include "dialog_controls.h"

// Named UTF-16 strings let the shared assembly callback use the Unicode controls.
const WCHAR PicoText_ui_error[] = L"窗口创建失败";
const WCHAR PicoText_click_error[] = L"点击失败";
const WCHAR PicoText_interval[] = L"间隔限1–60000ms";
const WCHAR PicoText_busy[] = L"F1 不可用";
const WCHAR PicoText_f1[] = L"F1 启停";
const WCHAR PicoText_on[] = L"连点中";
const WCHAR PicoText_timer_error[] = L"计时器启动失败";
const WCHAR PicoText_wait_error[] = L"计时失败";
const WCHAR PicoText_title[] = L"PicoClicker";

DWORD WINAPI PicoSelectedButton(HWND dialog)
{
    return SelectedMouseFlags(dialog);
}

void WINAPI PicoShowButton(HWND dialog, BOOL running)
{
    ShowLiteButton(dialog, running);
}
