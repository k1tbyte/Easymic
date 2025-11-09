    #ifndef EASYMIC_BASEWINDOW_HPP
    #define EASYMIC_BASEWINDOW_HPP

    #include <cstdio>
    #include <windows.h>
    #include <functional>
    #include <memory>
    #include <unordered_map>
    #include <type_traits>
    #include "WindowRegistry.hpp"
    #include "ViewModel/ViewModel.hpp"

    class IViewModel;

    template<typename T>
    concept IViewModelType = std::is_base_of_v<IViewModel, T> || std::is_same_v<IViewModel, T>;

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

        virtual void Invalidate() {
            if (_shadowHwnd) {
                SendMessage(hwnd_, WM_PAINT, 0, 0);
                return;
            }

            InvalidateRect(hwnd_, nullptr, TRUE);
        }

        virtual void Show() {
            _show(GetEffectiveHandle());
        }


        virtual void Hide() {
            _hide(GetEffectiveHandle());
        }

        virtual void Close() {
            _close(GetEffectiveHandle());
        }

        HINSTANCE GetHInstance() const {
            return hInstance_;
        }

        BaseWindow* GetParent() const {
            return _parent;
        }

        virtual std::shared_ptr<IViewModel> GetViewModel() const {
            return _viewModel;
        }

        virtual std::shared_ptr<BaseWindow> SetPositionX(LONG x) {
            this->_pos.x = x;
            return shared_from_this();
        }

        virtual std::shared_ptr<BaseWindow> SetPositionY(LONG y) {
            this->_pos.y = y;
            return shared_from_this();
        }

        virtual std::shared_ptr<BaseWindow> SetWidth(LONG width) {
            this->_size.x = width;
            return shared_from_this();
        }

        virtual std::shared_ptr<BaseWindow> SetHeight(LONG height) {
            this->_size.y = height;
            return shared_from_this();
        }

        virtual std::shared_ptr<BaseWindow> UpdateRect() {
            return _updateRect(GetEffectiveHandle());
        }

        virtual std::shared_ptr<BaseWindow> RefreshPos(HWND insertAfter) {
            return _refreshPos(GetEffectiveHandle(), insertAfter);
        }

        LONG GetPositionX() const {
            return _pos.x;
        }
        LONG GetPositionY() const {
            return _pos.y;
        }
        LONG GetWidth() const {
            return _size.x;
        }
        LONG GetHeight() const {
            return _size.y;
        }

        template <IViewModelType T, typename... Args>
        std::shared_ptr<T> AttachViewModel(Args &&... args) {
            auto vm = std::make_shared<T>(shared_from_this(), std::forward<Args>(args)...);
            _viewModel = vm;
            return vm;
        }

        HWND GetEffectiveHandle() const {
            return _shadowHwnd ? _shadowHwnd : hwnd_;
        }

        bool IsOvershadowed() const {
            return _shadowHwnd != nullptr;
        }

        void SetShadowHwnd(HWND hwnd) {
            _shadowHwnd = hwnd;
        }

    protected:
        BaseWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}

        /**
         * @brief Main message handler
         * Used to dispatch registered message handlers
         */
        virtual LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
            // Special handling for drag caption
            if (message == WM_NCHITTEST) {
                LRESULT result = DefWindowProcW(hwnd_, message, wParam, lParam);
                return result == HTCLIENT ? HTCAPTION : result;
            }

            // Search for registered handler
            if (const auto it = messageHandlers_.find(message); it != messageHandlers_.end()) {
                return it->second(wParam, lParam);
            }

            return DefWindowProcW(hwnd_, message, wParam, lParam);
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

                if ((message == WM_INITDIALOG || message == WM_CREATE) && lParam != 0) {
                    window = message == WM_INITDIALOG ?  reinterpret_cast<BaseWindow*>(lParam)
                                                       : static_cast<BaseWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
                    window->RegisterWindow(hwnd);
                    RECT rect;
                    GetWindowRect(hwnd, &rect);
                    window->SetHeight(rect.bottom - rect.top)
                        ->SetWidth(rect.right - rect.left)
                        ->SetPositionX(rect.left)
                        ->SetPositionY(rect.top);

                } else {
                    return DefWindowProcW(hwnd, message, wParam, lParam);
                }
            } else if (message == WM_DESTROY) {
                window->hwnd_ = nullptr;
                WindowRegistry::Instance().Unregister(hwnd);
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

        std::shared_ptr<BaseWindow> _updateRect(HWND hWnd) {
            if (!hWnd) {
                return shared_from_this();
            }

            RECT rect;
            GetWindowRect(hWnd, &rect);
            _size.x = rect.right - rect.left;
            _size.y = rect.bottom - rect.top;
            _pos.x = rect.left;
            _pos.y = rect.top;
            return shared_from_this();
        }

        std::shared_ptr<BaseWindow> _refreshPos(HWND hWnd, HWND insertAfter) {
            if (!hWnd) {
                return shared_from_this();
            }

            SetWindowPos(
                hWnd,
                insertAfter,
                _pos.x,
                _pos.y,
                _size.x,
                _size.y,
                SWP_FRAMECHANGED | SWP_NOACTIVATE
            );

            return shared_from_this();
        }

        void _close(HWND hWnd) {
            if (!hWnd) {
                return;
            }
            DestroyWindow(hWnd);
        }

        void _hide(HWND hWnd) {
            if (!hWnd || !isVisible_) {
                return;
            }
            ShowWindow(hWnd, SW_HIDE);
            isVisible_ = false;
        }

        void _show(HWND hWnd) {
            if (!hWnd || isVisible_) {
                return;
            }

            ShowWindow(hWnd, SW_SHOW);
            Invalidate();
            UpdateWindow(hWnd);
            isVisible_ = true;
        }

        POINT _size{};
        POINT _pos{};
        HWND hwnd_ = nullptr;
        HWND _shadowHwnd = nullptr;
        BaseWindow* _parent = nullptr;
        std::shared_ptr<IViewModel> _viewModel;
        HINSTANCE hInstance_ = nullptr;
        bool isVisible_ = false;
        std::unordered_map<UINT, MessageHandler> messageHandlers_;
    };

    #endif //EASYMIC_BASEWINDOW_HPP
