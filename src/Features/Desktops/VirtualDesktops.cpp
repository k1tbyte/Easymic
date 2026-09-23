#include "VirtualDesktops.hpp"

#include "definitions.h"

#include <mutex>
#include <objectarray.h>
#include <servprov.h>
#include <winstring.h>

namespace {

    // IInspectable in the shell, but only ever handed back to it - opaque is enough
    using IApplicationView = IUnknown;

    class DECLSPEC_UUID("C2F03A33-21F5-47FA-B4BB-156362A2F239") ImmersiveShell;
    class DECLSPEC_UUID("C5E0CDCA-7B6E-41B2-9FC4-D93975CC467B") DesktopManagerService;
    class DECLSPEC_UUID("B5A399E7-1C87-46B8-88E9-FC5747B171BD") PinnedAppsService;

    MIDL_INTERFACE("3F07F4BE-B107-441A-AF0F-39D82529072C")
    IVirtualDesktop : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IApplicationView* view, BOOL* visible) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetID(GUID* id) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* name) = 0;
    };

    // The 24H2 layout: same IID as 23H2, SwitchDesktopAndMoveForegroundView inserted at slot 7
    MIDL_INTERFACE("53F5CA0B-158F-4124-900C-057158060B27")
    IVirtualDesktopManagerInternal : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE GetCount(int* count) = 0;
        virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IApplicationView* view, IVirtualDesktop* desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IApplicationView* view, BOOL* can) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IVirtualDesktop* from, int direction, IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IVirtualDesktop* desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(IVirtualDesktop* desktop) = 0;
        // CreateDesktop in the shell - winuser.h owns that name as a macro
        virtual HRESULT STDMETHODCALLTYPE AddDesktop(IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE MoveDesktop(IVirtualDesktop* desktop, int index) = 0;
        virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(IVirtualDesktop* desktop, IVirtualDesktop* fallback) = 0;
        virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID* id, IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(IVirtualDesktop* desktop, IObjectArray** include, IObjectArray** exclude) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDesktopName(IVirtualDesktop* desktop, HSTRING name) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(IVirtualDesktop* desktop, HSTRING path) = 0;
        virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(HSTRING path) = 0;
        virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(IApplicationView* from, IApplicationView* to) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateRemoteDesktop(HSTRING path, IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE SwitchRemoteDesktop(IVirtualDesktop* desktop, void* type) = 0;
        virtual HRESULT STDMETHODCALLTYPE SwitchDesktopWithAnimation(IVirtualDesktop* desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetLastActiveDesktop(IVirtualDesktop** desktop) = 0;
        virtual HRESULT STDMETHODCALLTYPE WaitForAnimationToComplete() = 0;
    };

    MIDL_INTERFACE("1841C6D7-4F9D-42C0-AF41-8747538F10E5")
    IApplicationViewCollection : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE GetViews(IObjectArray** views) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(IObjectArray** views) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(LPCWSTR id, IObjectArray** views) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND window, IApplicationView** view) = 0;
    };

    MIDL_INTERFACE("4CE81583-1E4C-4632-A621-07A53543148F")
    IVirtualDesktopPinnedApps : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(LPCWSTR id, BOOL* pinned) = 0;
        virtual HRESULT STDMETHODCALLTYPE PinAppID(LPCWSTR id) = 0;
        virtual HRESULT STDMETHODCALLTYPE UnpinAppID(LPCWSTR id) = 0;
        virtual HRESULT STDMETHODCALLTYPE IsViewPinned(IApplicationView* view, BOOL* pinned) = 0;
        virtual HRESULT STDMETHODCALLTYPE PinView(IApplicationView* view) = 0;
        virtual HRESULT STDMETHODCALLTYPE UnpinView(IApplicationView* view) = 0;
    };

    // The documented one - the SDK hides its declaration behind NTDDI guards this build misses
    class DECLSPEC_UUID("AA509086-5CA9-4C25-8F95-589D3C07B48A") WindowDesktopsClass;
    MIDL_INTERFACE("A5CD92FF-29BE-454C-8D04-D82879FB3F1B")
    IWindowDesktops : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(HWND window, BOOL* onCurrent) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND window, GUID* id) = 0;
    };

    /// All or none, so a caller holding a Desktops pointer can use the rest.
    struct Shell {
        ComPtr<IVirtualDesktopManagerInternal> Desktops;
        ComPtr<IApplicationViewCollection> Views;
        ComPtr<IVirtualDesktopPinnedApps> Pinned;
        ComPtr<IWindowDesktops> Windows;
    };

    // Never destroyed: a threadpool placement can still be running while statics tear down at exit
    std::mutex& _lock = *new std::mutex;
    Shell& _shell = *new Shell;

    Shell _connect() {
        ComPtr<IServiceProvider> provider;
        Shell shell;
        if (FAILED(CoCreateInstance(__uuidof(ImmersiveShell), nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(&provider)))
            || FAILED(provider->QueryService(__uuidof(DesktopManagerService), IID_PPV_ARGS(&shell.Desktops)))
            || FAILED(provider->QueryService(__uuidof(IApplicationViewCollection), IID_PPV_ARGS(&shell.Views)))
            || FAILED(provider->QueryService(__uuidof(PinnedAppsService), IID_PPV_ARGS(&shell.Pinned)))
            || FAILED(CoCreateInstance(__uuidof(WindowDesktopsClass), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(&shell.Windows)))) {
            return {};
        }
        return shell;
    }

    /// Reconnects only if nobody replaced the stale connection already.
    Shell _acquire(const IVirtualDesktopManagerInternal* stale) {
        std::lock_guard lock(_lock);
        if (_shell.Desktops.Get() == stale) {
            _shell = _connect();
        }
        return _shell;
    }

    bool _disconnected(const HRESULT hr) {
        return hr == RPC_E_DISCONNECTED || hr == RPC_E_SERVER_DIED || hr == RPC_E_SERVER_DIED_DNE
               || hr == CO_E_OBJNOTCONNECTED || hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);
    }

    /// The proxies die with explorer, so a call that hits a dead one reconnects and runs once more.
    template<typename Body>
    bool _call(Body&& body) {
        if (!VirtualDesktops::Supported()) {
            return false;
        }

        const IVirtualDesktopManagerInternal* stale = nullptr;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const Shell shell = _acquire(stale);
            if (!shell.Desktops) {
                return false;
            }
            const HRESULT hr = body(shell);
            if (!_disconnected(hr)) {
                return SUCCEEDED(hr);
            }
            stale = shell.Desktops.Get();
        }
        return false;
    }

    HRESULT _desktopAt(const Shell& shell, const int index, IVirtualDesktop** desktop) {
        if (index < 0) {
            return E_INVALIDARG;
        }
        ComPtr<IObjectArray> desktops;
        const HRESULT hr = shell.Desktops->GetDesktops(&desktops);
        return SUCCEEDED(hr) ? desktops->GetAt(index, IID_PPV_ARGS(desktop)) : hr;
    }

    /// Position of the desktop with that id; index stays -1 for an id no desktop has.
    HRESULT _indexOf(const Shell& shell, const GUID& id, int& index) {
        ComPtr<IObjectArray> desktops;
        UINT count = 0;
        HRESULT hr = shell.Desktops->GetDesktops(&desktops);
        if (SUCCEEDED(hr)) hr = desktops->GetCount(&count);

        for (UINT i = 0; SUCCEEDED(hr) && i < count; ++i) {
            ComPtr<IVirtualDesktop> desktop;
            GUID candidate{};
            hr = desktops->GetAt(i, IID_PPV_ARGS(&desktop));
            if (SUCCEEDED(hr)) hr = desktop->GetID(&candidate);
            if (SUCCEEDED(hr) && candidate == id) {
                index = static_cast<int>(i);
                break;
            }
        }
        return hr;
    }

} // anonymous namespace

namespace VirtualDesktops {

    bool Supported() {
        // 26100-26999 is the 24H2/25H2 branch. The next one gets its own check, never a guess
        static const bool known = [] {
            wchar_t build[16]{};
            DWORD size = sizeof(build);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                             L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &size) != ERROR_SUCCESS) {
                return false;
            }
            const long number = wcstol(build, nullptr, 10);
            return number >= 26100 && number < 27000;
        }();
        return known;
    }

    int Count() {
        int count = 0;
        _call([&](const Shell& shell) { return shell.Desktops->GetCount(&count); });
        return count;
    }

    int Current() {
        int current = -1;
        _call([&](const Shell& shell) {
            ComPtr<IVirtualDesktop> desktop;
            GUID id{};
            HRESULT hr = shell.Desktops->GetCurrentDesktop(&desktop);
            if (SUCCEEDED(hr)) hr = desktop->GetID(&id);
            return SUCCEEDED(hr) ? _indexOf(shell, id, current) : hr;
        });
        return current;
    }

    int DesktopOf(const HWND window) {
        int index = -1;
        _call([&](const Shell& shell) {
            GUID id{};
            const HRESULT hr = shell.Windows->GetWindowDesktopId(window, &id);
            return SUCCEEDED(hr) ? _indexOf(shell, id, index) : hr;
        });
        return index;
    }

    std::wstring Name(const int index) {
        std::wstring name;
        _call([&](const Shell& shell) {
            ComPtr<IVirtualDesktop> desktop;
            HSTRING text = nullptr;
            HRESULT hr = _desktopAt(shell, index, &desktop);
            if (SUCCEEDED(hr)) hr = desktop->GetName(&text);
            if (SUCCEEDED(hr)) {
                name = WindowsGetStringRawBuffer(text, nullptr);
                WindowsDeleteString(text);
            }
            return hr;
        });
        return name.empty() ? L"Desktop " + std::to_wstring(index + 1) : name;
    }

    bool Rename(const int index, const std::wstring& name) {
        return _call([&](const Shell& shell) {
            ComPtr<IVirtualDesktop> desktop;
            HSTRING text = nullptr;
            HRESULT hr = _desktopAt(shell, index, &desktop);
            if (SUCCEEDED(hr)) hr = WindowsCreateString(name.c_str(), static_cast<UINT32>(name.size()), &text);
            if (SUCCEEDED(hr)) {
                hr = shell.Desktops->SetDesktopName(desktop.Get(), text);
                WindowsDeleteString(text);
            }
            return hr;
        });
    }

    bool Grow(const int count) {
        return _call([&](const Shell& shell) {
            int have = 0;
            HRESULT hr = shell.Desktops->GetCount(&have);
            for (; SUCCEEDED(hr) && have < count; ++have) {
                ComPtr<IVirtualDesktop> added;
                hr = shell.Desktops->AddDesktop(&added);
            }
            return hr;
        });
    }

    bool Switch(const int index) {
        return _call([&](const Shell& shell) {
            ComPtr<IVirtualDesktop> desktop;
            HRESULT hr = _desktopAt(shell, index, &desktop);
            // Back-to-back presses queue behind the running animation
            if (SUCCEEDED(hr)) hr = shell.Desktops->WaitForAnimationToComplete();
            if (SUCCEEDED(hr)) hr = shell.Desktops->SwitchDesktopWithAnimation(desktop.Get());
            return hr;
        });
    }

    bool MoveWindow(const HWND window, const int index) {
        return _call([&](const Shell& shell) {
            ComPtr<IApplicationView> view;
            ComPtr<IVirtualDesktop> desktop;
            HRESULT hr = shell.Views->GetViewForHwnd(window, &view);
            if (SUCCEEDED(hr)) hr = _desktopAt(shell, index, &desktop);
            if (SUCCEEDED(hr)) hr = shell.Desktops->MoveViewToDesktop(view.Get(), desktop.Get());
            return hr;
        });
    }

    bool TogglePinned(const HWND window) {
        return _call([&](const Shell& shell) {
            ComPtr<IApplicationView> view;
            BOOL pinned = FALSE;
            HRESULT hr = shell.Views->GetViewForHwnd(window, &view);
            if (SUCCEEDED(hr)) hr = shell.Pinned->IsViewPinned(view.Get(), &pinned);
            if (SUCCEEDED(hr)) hr = pinned ? shell.Pinned->UnpinView(view.Get()) : shell.Pinned->PinView(view.Get());
            return hr;
        });
    }
}
