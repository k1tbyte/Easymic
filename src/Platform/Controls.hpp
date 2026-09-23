#pragma once

#include <windows.h>

namespace Controls {

    /// A child of parent laid out in dialog's units and font - dialog may be parent itself, or the
    /// dialog a plain window inside it borrows its scale from.
    inline HWND Create(HWND dialog, HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                       RECT cell, int id, DWORD exStyle = 0) {
        MapDialogRect(dialog, &cell);
        HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, cell.left, cell.top,
                                       cell.right - cell.left, cell.bottom - cell.top, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), FALSE);
        return control;
    }
}
