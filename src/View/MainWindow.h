#ifndef EASYMIC_MAINWINDOW_H
#define EASYMIC_MAINWINDOW_H

#include <windows.h>
#include <unordered_map>
#include <cstdio>
#include "../Resources/Resource.h"

class MainWindow;

typedef void (MainWindow::*WindowEvent)(WPARAM, LPARAM);

class MainWindow final {
    HINSTANCE hInst;
    HICON appIcon;

private:
    friend LRESULT CALLBACK WindowHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void OnCreate(WPARAM wParam, LPARAM lParam);
    void OnDestroy(WPARAM wParam, LPARAM lParam);
    void OnPaint(WPARAM wParam, LPARAM lParam);
    void OnClose(WPARAM wParam, LPARAM lParam);

public:
    HWND hWnd;
    MainWindow(HINSTANCE hInstance);
    bool InitWindow(LPCWSTR name);

    void Show(int cmdShow) {
        ShowWindow(hWnd,cmdShow);
    }

    void Hide() {
        ShowWindow(hWnd,SW_HIDE);
    }

private:

    const std::unordered_map<UINT,WindowEvent> events = {
            {WM_CREATE, &MainWindow::OnCreate },
            {WM_DESTROY, &MainWindow::OnDestroy },
            {WM_PAINT, &MainWindow::OnPaint },
            {WM_CLOSE, &MainWindow::OnClose }
    };
};


#endif //EASYMIC_MAINWINDOW_H
