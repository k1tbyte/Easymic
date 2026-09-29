#pragma once

#include <windows.h>
#include <commctrl.h>
#include <initializer_list>

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

    /// Splits a report list's client width by percent, the last column taking the rest. Call after a fill:
    /// a vertical scroll bar that appeared narrows the client.
    inline void FitColumns(HWND list, std::initializer_list<int> percents) {
        RECT client;
        GetClientRect(list, &client);
        int rest = client.right, column = 0;
        for (const int percent : percents) {
            const int width = client.right * percent / 100;
            ListView_SetColumnWidth(list, column++, width);
            rest -= width;
        }
        ListView_SetColumnWidth(list, column, rest);
    }
}
