#ifndef PICOCLICKER_RESOURCE_H
#define PICOCLICKER_RESOURCE_H

// Select the same labels in C and in the dialog resource. RC encodes its own strings.
#if defined(PICOCLICKER_ASCII)
#define PICO_TEXT(localized, english) english
#elif defined(RC_INVOKED)
#define PICO_TEXT(localized, english) localized
#else
#define PICO_TEXT(localized, english) TEXT(localized)
#endif

#define IDD_MAIN 101

// Keep the interactive IDs contiguous and below 128 for short x86 immediates.
#define IDC_INTERVAL 10
#define IDC_LEFT 11
#define IDC_MIDDLE 12
#define IDC_RIGHT 13
#define IDC_TOGGLE 14
#define IDC_STATUS 15
#define IDC_HOTKEY 16

#define ID_HOTKEY 1
#define ID_HOTKEY_ALTERNATE 2
#define ID_CAPTURE_TIMER 3

#endif
