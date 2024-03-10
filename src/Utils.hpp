#ifndef EASYMIC_UTILS_HPP
#define EASYMIC_UTILS_HPP

#include <windows.h>
#include <commctrl.h>

namespace Utils {

    static void CenterWindowOnScreen(HWND hWnd)
    {
        HWND hWndDesktop = GetDesktopWindow();
        RECT rcDlg, rcOwner, rc;

        GetWindowRect(hWndDesktop, &rcOwner);
        GetWindowRect(hWnd, &rcDlg);
        CopyRect(&rc, &rcOwner);

        OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
        OffsetRect(&rc, -rc.left, -rc.top);
        OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

        SetWindowPos(hWnd, HWND_TOP,
                     rcOwner.left + (rc.right / 2),
                     rcOwner.top + (rc.bottom / 2), 0, 0, SWP_NOSIZE
        );
    }

    static void InitTrackbar(HWND hWnd, LPARAM pageSize, LPARAM minMax, LPARAM value)
    {
        SendMessage(hWnd, TBM_SETRANGE,
                    (WPARAM) TRUE,  // redraw flag
                    minMax);

        SendMessage(hWnd, TBM_SETPAGESIZE,
                    0, pageSize);                  // new page size

        SendMessage(hWnd, TBM_SETPOS, (WPARAM) TRUE, value);
    }

    static void LoadResource(HINSTANCE hInst,LPCSTR resourceName, LPCSTR resourceType, BYTE** buffer, DWORD* size)
    {
        HRSRC hResInfo = FindResource(hInst,resourceName, resourceType);
        HANDLE hRes = LoadResource(hInst, hResInfo);
        *buffer = (BYTE*)LockResource(hRes);
        *size = SizeofResource(hInst, hResInfo);
    }
}

#endif //EASYMIC_UTILS_HPP
