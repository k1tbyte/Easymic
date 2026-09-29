#include "Tracker.hpp"

#include "AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Core/Tray.hpp"
#include "definitions.h"
#include "Gfx/Gdi.hpp"
#include "Str.hpp"
#include "Gfx/TrayIconTheme.hpp"
#include "VirtualDesktops.hpp"

#include <atomic>
#include <mutex>
#include <utility>

namespace {

    constexpr auto DesktopsKey = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops)";

    // Never destroyed: the wait can still be queued while statics tear down at exit
    struct Watch {
        HKEY Key = nullptr;
        HANDLE Signal = nullptr;
        // Callbacks overlap once the notify is re-armed; Serial keeps them in order
        std::mutex Serial;
        GUID Current{};
        std::atomic<int> CurrentIndex = -1;
        std::mutex NameLock;
        std::wstring Name;
    };

    Watch& _watch = *new Watch;
    const Feedback* _feedback = nullptr;
    const DesktopSettings* _settings = nullptr;
    HICON _icon = nullptr;
    int _iconNumber = 0;

    bool _readGuid(const wchar_t* value, GUID& id) {
        DWORD size = sizeof(id);
        return RegGetValueW(_watch.Key, nullptr, value, RRF_RT_REG_BINARY, nullptr, &id, &size)
               == ERROR_SUCCESS;
    }

    /// Threadpool, under Serial.
    void _refresh(const bool switched) {
        const int index = VirtualDesktops::Current();
        const std::wstring name = index >= 0 ? VirtualDesktops::Name(index) : L"";
        {
            std::lock_guard lock(_watch.NameLock);
            _watch.Name = name;
        }
        _watch.CurrentIndex = index;

        if (switched) {
            // The config belongs to the UI thread
            Dispatcher::ToUi([name] {
                if (_settings->AnnounceSwitch) {
                    _feedback->Post(Str::WideToUtf8(name));
                }
            });
        }
        Tray::Changed();
    }

    void CALLBACK _signaled(void*, BOOLEAN) {
        std::lock_guard serial(_watch.Serial);
        GUID current{};
        const bool switched = _readGuid(L"CurrentVirtualDesktop", current)
                              && !IsEqualGUID(current, _watch.Current);
        if (switched) {
            _watch.Current = current;
        }

        // A notify registration fires once - re-arm first, or a change during _refresh is lost
        RegNotifyChangeKeyValue(_watch.Key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                _watch.Signal, TRUE);
        _refresh(switched);
    }

    HICON _digitIcon(const int number) {
        if (number < 1) {
            return nullptr;
        }
        if (_icon && _iconNumber == number) {
            return _icon;
        }

        Gdiplus::Bitmap bitmap(32, 32, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics canvas(&bitmap);
            canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            canvas.Clear(Gdiplus::Color(0, 0, 0, 0));
            const bool light = TrayIconTheme::IsLightTaskbar();
            const Gdiplus::Color colour(255, light ? 32 : 240, light ? 32 : 240, light ? 36 : 240);
            const Gdiplus::FontFamily family(L"Segoe UI");
            const Gdiplus::Font bold(&family, 20.f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            const std::wstring digit = std::to_wstring(number);
            Gdiplus::RectF box(0, 0, 32, 32);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            const Gdiplus::SolidBrush brush(colour);
            canvas.DrawString(digit.c_str(), -1, &bold, box, &format, &brush);
        }

        HICON icon = nullptr;
        bitmap.GetHICON(&icon);
        DestroyIcon(std::exchange(_icon, icon));
        _iconNumber = number;
        return _icon;
    }

    HICON _trayIcon() {
        return _digitIcon(_watch.CurrentIndex + 1);
    }

    std::wstring _tooltip() {
        std::lock_guard lock(_watch.NameLock);
        return _watch.Name.empty() ? std::wstring(APP_NAME) : APP_NAME L" - " + _watch.Name;
    }

    void _themeChanged() {
        DestroyIcon(std::exchange(_icon, nullptr));
    }

} // anonymous namespace

namespace Tracker {

    void Start(const Feedback& feedback, const DesktopSettings& settings) {
        if (!VirtualDesktops::Supported()) {
            return;
        }
        _feedback = &feedback;
        _settings = &settings;

        Tray::Add({.Id = "vd.desktop",
                   .Title = L"Current desktop",
                   .Icon = &_trayIcon,
                   .Tooltip = &_tooltip,
                   .ThemeChanged = &_themeChanged});

        if (RegOpenKeyExW(HKEY_CURRENT_USER, DesktopsKey, 0, KEY_NOTIFY | KEY_READ, &_watch.Key) != ERROR_SUCCESS) {
            return;
        }
        _readGuid(L"CurrentVirtualDesktop", _watch.Current);
        _watch.Signal = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE wait = nullptr;
        if (!_watch.Signal
            || RegNotifyChangeKeyValue(_watch.Key, TRUE,
                                       REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                       _watch.Signal, TRUE) != ERROR_SUCCESS
            || !RegisterWaitForSingleObject(&wait, _watch.Signal, _signaled, nullptr,
                                            INFINITE, WT_EXECUTEDEFAULT)) {
            return;
        }

        TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void*) {
            std::lock_guard serial(_watch.Serial);
            _refresh(false);
        }, nullptr, nullptr);
    }
}
