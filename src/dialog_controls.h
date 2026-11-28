#ifndef PICOCLICKER_DIALOG_CONTROLS_H
#define PICOCLICKER_DIALOG_CONTROLS_H

#include <windows.h>
#include "resource.h"

static inline void SetControlText(HWND dialog, int id, LPCTSTR text)
{
    // The edit, radio buttons and text updates share one control-message import.
    SendDlgItemMessage(dialog, id, WM_SETTEXT, 0, (LPARAM)text);
}

static inline DWORD SelectedMouseFlags(HWND dialog)
{
    DWORD down = MOUSEEVENTF_LEFTDOWN;
    for (int id = IDC_MIDDLE; id <= IDC_RIGHT; ++id)
    {
        if (SendDlgItemMessage(dialog, id, BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            down = id == IDC_MIDDLE ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_RIGHTDOWN;
            break;
        }
    }
    return down;
}

#ifdef PICOCLICKER_LITE
static inline void ShowLiteButton(HWND dialog, BOOL running)
{
    // The interval and radio IDs are contiguous. Keep Stop available during a run.
    for (int id = IDC_INTERVAL; id <= IDC_RIGHT; ++id)
    {
        EnableWindow(GetDlgItem(dialog, id), !running);
    }
    SetControlText(dialog, IDC_TOGGLE, running ? PICO_TEXT("停止", "Stop") : PICO_TEXT("开始", "Start"));
}
#endif

#endif
