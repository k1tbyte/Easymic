#include "SettingsWindow.hpp"

#include <commctrl.h>
#include <uxtheme.h>

#include "../../Resources/Resource.h"

SettingsWindow::SettingsWindow(HINSTANCE hInstance)
    : BaseWindow(hInstance)
{
}

bool SettingsWindow::Initialize(const Config& config) {
    config_ = config;
    SetupMessageHandlers();
    return true;
}

void SettingsWindow::SetupMessageHandlers() {
    RegisterMessageHandler(WM_INITDIALOG, [this](WPARAM wParam, LPARAM lParam) {
        return OnInitDialog(wParam, lParam);
    });

    RegisterMessageHandler(WM_COMMAND, [this](WPARAM wParam, LPARAM lParam) {
        return OnCommand(wParam, lParam);
    });

    RegisterMessageHandler(WM_DESTROY, [this](WPARAM wParam, LPARAM lParam) {
        return OnDestroy(wParam, lParam);
    });
}

void SettingsWindow::Show() {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        isVisible_ = true;
        return;
    }

    hwnd_ = CreateDialogParam(
        hInstance_,
        MAKEINTRESOURCE(config_.dialogResourceId),
        config_.parentHwnd,
        StaticWindowProc,
        reinterpret_cast<LPARAM>(this)
    );

    if (!hwnd_) {
        return;
    }

    ShowWindow(hwnd_, SW_SHOW);
    isVisible_ = true;
}

void SettingsWindow::Hide() {
    if (!hwnd_) {
        return;
    }

    DestroyWindow(hwnd_);
}

#pragma region Shit

#define PR_CALC_PERCENTVAL(percent, total_length)((total_length * percent) / 100)
#define I_DEFAULT -8

LRESULT _r_wnd_sendmessage (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id,
    _In_ UINT msg,
     WPARAM wparam,
     LPARAM lparam
)
{
    LRESULT val;

    if (ctrl_id)
    {
        val = SendDlgItemMessageW (hwnd, ctrl_id, msg, wparam, lparam);
    }
    else
    {
        val = SendMessageW (hwnd, msg, wparam, lparam);
    }

    return val;
}

FORCEINLINE HWND _r_ctrl_getdlgitem (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id
)
{
    if (ctrl_id)
    {
        return GetDlgItem (hwnd, ctrl_id);
    }
    else
    {
        return hwnd;
    }
}

FORCEINLINE BOOLEAN _r_wnd_top (
    _In_ HWND hwnd,
    _In_ BOOLEAN is_enable
)
{
    return !!SetWindowPos (
        hwnd,
        is_enable ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
    );
}

VOID _r_ctrl_settipstyle (
    _In_ HWND htip,
    _In_ BOOLEAN is_instant
)
{
    // 0 means instant, default is -1
    _r_wnd_sendmessage (htip, 0, TTM_SETDELAYTIME, TTDT_INITIAL, is_instant ? 0 : -1);

    _r_wnd_sendmessage (htip, 0, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAXSHORT);

    // allow newlines (-1 doesn't work)
    _r_wnd_sendmessage (htip, 0, TTM_SETMAXTIPWIDTH, 0, MAXSHORT);

    _r_wnd_top (htip, TRUE); // HACK!!!
}

FORCEINLINE HWND _r_listview_gettooltips (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id
)
{
    return (HWND)_r_wnd_sendmessage (hwnd, ctrl_id, LVM_GETTOOLTIPS, 0, 0);
}

VOID _r_listview_setstyle (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id,
    _In_opt_ ULONG ex_style,
    _In_ BOOL is_groupview
)
{
    HWND hctrl;

    hctrl = _r_ctrl_getdlgitem (hwnd, ctrl_id);

    if (hctrl) {
        SetWindowTheme (hctrl, L"Explorer", NULL);
}

    hctrl = _r_listview_gettooltips (hwnd, ctrl_id);

    if (hctrl)
        _r_ctrl_settipstyle (hctrl, FALSE);

    if (ex_style)
        _r_wnd_sendmessage (hwnd, ctrl_id, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)ex_style);

    _r_wnd_sendmessage (hwnd, ctrl_id, LVM_ENABLEGROUPVIEW, (WPARAM)is_groupview, 0);
}

LONG _r_ctrl_getwidth (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id
)
{
    RECT rect;
    HWND htarget;

    htarget = _r_ctrl_getdlgitem (hwnd, ctrl_id);

    if (!htarget)
        return 0;

    if (GetClientRect (htarget, &rect))
        return rect.right;

    return 0;
}


INT _r_listview_addcolumn (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id,
    _In_ INT column_id,
    _In_opt_ LPWSTR title,
    _In_opt_ INT width,
    _In_opt_ INT fmt
)
{
    LVCOLUMNW lvc = {0};
    LONG client_width;

    lvc.mask = LVCF_SUBITEM;
    lvc.iSubItem = column_id;

    if (title)
    {
        lvc.mask |= LVCF_TEXT;

        lvc.pszText = title;
    }

    if (width)
    {
        if (width < LVSCW_AUTOSIZE_USEHEADER)
        {
            client_width = _r_ctrl_getwidth (hwnd, ctrl_id);

            if (client_width)
            {
                width = PR_CALC_PERCENTVAL (-width, client_width);
            }
            else
            {
                width = -width;
            }
        }

        lvc.mask |= LVCF_WIDTH;

        lvc.cx = width;
    }

    if (fmt)
    {
        lvc.mask |= LVCF_FMT;

        lvc.fmt = fmt;
    }

    return (INT)_r_wnd_sendmessage (hwnd, ctrl_id, LVM_INSERTCOLUMN, (WPARAM)column_id, (LPARAM)&lvc);
}

INT _r_listview_addgroup (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id,
    _In_ INT group_id,
    _In_opt_ LPWSTR title,
    _In_opt_ UINT align,
    _In_opt_ UINT state,
    _In_opt_ UINT state_mask
)
{
    LVGROUP lvg = {0};

    lvg.cbSize = sizeof (lvg);
    lvg.mask = LVGF_GROUPID;
    lvg.iGroupId = group_id;

    if (title)
    {
        lvg.mask |= LVGF_HEADER;

        lvg.pszHeader = title;
    }

    if (align)
    {
        lvg.mask |= LVGF_ALIGN;

        lvg.uAlign = align;
    }

    if (state || state_mask)
    {
        lvg.mask |= LVGF_STATE;

        lvg.state = state;
        lvg.stateMask = state_mask;
    }

    return (INT)_r_wnd_sendmessage (hwnd, ctrl_id, LVM_INSERTGROUP, (WPARAM)group_id, (LPARAM)&lvg);
}

INT _r_listview_additem (
    _In_ HWND hwnd,
    _In_opt_ INT ctrl_id,
    _In_ INT item_id,
    _In_opt_ LPWSTR string,
    _In_ INT image_id,
    _In_ INT group_id,
    _In_ LPARAM lparam
)
{
    LVITEMW lvi = {0};

    lvi.iItem = item_id;
    lvi.iSubItem = 0;

    if (string)
    {
        lvi.mask |= LVIF_TEXT;

        lvi.pszText = string;
    }

    if (image_id != I_DEFAULT)
    {
        lvi.mask |= LVIF_IMAGE;

        lvi.iImage = image_id;
    }

    if (group_id != I_DEFAULT)
    {
        lvi.mask |= LVIF_GROUPID;

        lvi.iGroupId = group_id;
    }

    if (lparam != I_DEFAULT)
    {
        lvi.mask |= LVIF_PARAM;

        lvi.lParam = lparam;
    }

    return (INT)_r_wnd_sendmessage (hwnd, ctrl_id, LVM_INSERTITEM, 0, (LPARAM)&lvi);
}



#pragma endregion

LRESULT SettingsWindow::OnInitDialog(WPARAM wParam, LPARAM lParam) {


    // Center window on screen
    RECT rect;
    GetWindowRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const int posX = (screenWidth - width) / 2;
    const int posY = (screenHeight - height) / 2;

    SetWindowPos(hwnd_, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    return TRUE;
}

LRESULT SettingsWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    const UINT commandId = LOWORD(wParam);

    if (commandId == IDOK || commandId == IDCANCEL) {
        Close();
        return 0;
    }

    return 0;
}

LRESULT SettingsWindow::OnDestroy(WPARAM wParam, LPARAM lParam) {
    hwnd_ = nullptr;
    isVisible_ = false;
    return 0;
}