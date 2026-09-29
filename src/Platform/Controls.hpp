#pragma once

#include <windows.h>
#include <commctrl.h>
#include <initializer_list>
#include <string>

namespace Controls {

    /// Laid out in dialog's units and font; dialog may be the parent or the dialog a plain window borrows its scale from.
    inline HWND Create(HWND dialog, HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                       RECT cell, int id, DWORD exStyle = 0) {
        MapDialogRect(dialog, &cell);
        HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, cell.left, cell.top,
                                       cell.right - cell.left, cell.bottom - cell.top, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), FALSE);
        return control;
    }

    inline std::wstring Text(HWND control) {
        std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
        text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
        return text;
    }

    /// A tab-walkable container window; null when its class cannot be registered.
    inline HWND Panel(HWND page, const RECT& cell, int id, const wchar_t* className, WNDPROC proc) {
        const WNDCLASSW windowClass{.lpfnWndProc = proc, .hInstance = GetModuleHandleW(nullptr),
                                    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
                                    .hbrBackground = GetSysColorBrush(COLOR_BTNFACE), .lpszClassName = className};
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return nullptr;
        }
        return Create(page, page, className, L"", WS_CLIPCHILDREN, cell, id, WS_EX_CONTROLPARENT);
    }

    /// Call after a fill: a vertical scroll bar that appeared narrows the client.
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
