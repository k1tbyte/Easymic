#ifndef EASYMIC_BASEWINDOW_HPP
#define EASYMIC_BASEWINDOW_HPP

#include <cstdio>
#include <windows.h>
#include <functional>
#include <unordered_map>
#include "WindowRegistry.hpp"

/**
 * @brief Базовый класс для всех окон приложения
 * Использует callback-based подход для обработки сообщений
 */
class BaseWindow {
public:
    using MessageHandler = std::function<LRESULT(WPARAM, LPARAM)>;

    virtual ~BaseWindow() {
        if (hwnd_) {
            WindowRegistry::Instance().Unregister(hwnd_);
        }
    }

    HWND GetHandle() const { return hwnd_; }
    HINSTANCE GetInstance() const { return hInstance_; }
    bool IsVisible() const { return isVisible_; }

    virtual void Show() {
        if (!hwnd_ || isVisible_) {
            return;
        }
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        isVisible_ = true;
    }

    virtual void Hide() {
        if (!hwnd_ || !isVisible_) {
            return;
        }
        ShowWindow(hwnd_, SW_HIDE);
        isVisible_ = false;
    }

    virtual void Close() {
        if (!hwnd_) {
            return;
        }
        DestroyWindow(hwnd_);
    }

protected:
    BaseWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}

    /**
     * @brief Главный обработчик оконных сообщений
     * Использует зарегистрированные callback'и для обработки
     */
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        // Специальная обработка для drag caption
        if (message == WM_NCHITTEST) {
            LRESULT result = DefWindowProc(hwnd_, message, wParam, lParam);
            return result == HTCLIENT ? HTCAPTION : result;
        }

        // Поиск зарегистрированного обработчика
        if (const auto it = messageHandlers_.find(message); it != messageHandlers_.end()) {
            return it->second(wParam, lParam);
        }

        return DefWindowProc(hwnd_, message, wParam, lParam);
    }

    /**
     * @brief Регистрация обработчика для конкретного сообщения
     */
    void RegisterMessageHandler(UINT message, MessageHandler handler) {
        messageHandlers_[message] = std::move(handler);
    }

    /**
     * @brief Статическая процедура окна для WinAPI
     */
    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        BaseWindow* window = WindowRegistry::Instance().Get(hwnd);

        if (!window) {

            if (message == WM_INITDIALOG && lParam != 0) {
                window = reinterpret_cast<BaseWindow*>(lParam);
                window->RegisterWindow(hwnd);
            } else {
                return DefWindowProc(hwnd, message, wParam, lParam);
            }
        }

        return window->HandleMessage(message, wParam, lParam);
    }

    /**
     * @brief Регистрация окна в реестре
     */
    void RegisterWindow(HWND hwnd) {
        hwnd_ = hwnd;
        WindowRegistry::Instance().Register(hwnd, this);
    }

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    bool isVisible_ = false;
    std::unordered_map<UINT, MessageHandler> messageHandlers_;
};

#endif //EASYMIC_BASEWINDOW_HPP
