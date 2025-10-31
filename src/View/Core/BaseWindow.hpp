#ifndef EASYMIC_BASEWINDOW_HPP
#define EASYMIC_BASEWINDOW_HPP

#include <cstdio>
#include <windows.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include "WindowRegistry.hpp"

class IBaseViewModel;
/**
 * @brief Base class for all application windows
 * Uses callback-based approach for message handling
 */
class BaseWindow : public std::enable_shared_from_this<BaseWindow> {
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

    virtual std::shared_ptr<IBaseViewModel> GetViewModel() const {
        return _viewModel;
    }


    template <typename T, typename... Args>
    std::shared_ptr<T> AttachViewModel(Args &&... args) {
        auto vm = std::make_shared<T>(shared_from_this(), std::forward<Args>(args)...);
        _viewModel = vm;
        return vm;
    }

protected:
    BaseWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}

    /**
     * @brief Main message handler
     * Used to dispatch registered message handlers
     */
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        // Special handling for drag caption
        if (message == WM_NCHITTEST) {
            LRESULT result = DefWindowProc(hwnd_, message, wParam, lParam);
            return result == HTCLIENT ? HTCAPTION : result;
        }

        // Search for registered handler
        if (const auto it = messageHandlers_.find(message); it != messageHandlers_.end()) {
            return it->second(wParam, lParam);
        }

        return DefWindowProc(hwnd_, message, wParam, lParam);
    }

    /**
     * @brief Register handler for specific message
     */
    void RegisterMessageHandler(UINT message, MessageHandler handler) {
        messageHandlers_[message] = std::move(handler);
    }

    /**
     * @brief Static window procedure for WinAPI
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
     * @brief Register window in registry
     */
    void RegisterWindow(HWND hwnd) {
        hwnd_ = hwnd;
        WindowRegistry::Instance().Register(hwnd, this);
    }

    HWND hwnd_ = nullptr;
    std::shared_ptr<IBaseViewModel> _viewModel;
    HINSTANCE hInstance_ = nullptr;
    bool isVisible_ = false;
    std::unordered_map<UINT, MessageHandler> messageHandlers_;
};

#endif //EASYMIC_BASEWINDOW_HPP
