#include "Accessibility.hpp"

#include <cwchar>
#include <oleacc.h>

#include "definitions.h"

namespace Accessibility {

namespace {

    /// A hung or slow app must not hold the edit lane: past this the field counts as not a password
    constexpr UINT TimeoutMs = 50;

    /// Resolved on the first call so an idle app maps neither DLL
    struct Api {
        decltype(&ObjectFromLresult) FromLresult = nullptr;
        decltype(&VariantClear) Clear = nullptr;
    };

    const Api& _api() {
        static const Api api = [] {
            const HMODULE oleacc = LoadLibraryW(L"oleacc.dll");
            const HMODULE oleaut = LoadLibraryW(L"oleaut32.dll");
            if (!oleacc || !oleaut) {
                return Api{};
            }
            return Api{reinterpret_cast<decltype(&ObjectFromLresult)>(GetProcAddress(oleacc, "ObjectFromLresult")),
                       reinterpret_cast<decltype(&VariantClear)>(GetProcAddress(oleaut, "VariantClear"))};
        }();
        return api;
    }

    /// A plain Win32 edit answers no WM_GETOBJECT; its style says it.
    bool _passwordEdit(const HWND window) {
        wchar_t name[64]{};
        CharLowerBuffW(name, static_cast<DWORD>(GetClassNameW(window, name, 64)));
        return std::wcsstr(name, L"edit") && (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD);
    }

    bool _protected(IAccessible& root, const Api& api) {
        VARIANT child{};
        child.vt = VT_I4;
        child.lVal = CHILDID_SELF;
        ComPtr<IAccessible> element;
        VARIANT focus{};
        if (SUCCEEDED(root.get_accFocus(&focus))) {
            if (focus.vt == VT_DISPATCH && focus.pdispVal) {
                focus.pdispVal->QueryInterface(IID_PPV_ARGS(&element));
            } else if (focus.vt == VT_I4) {
                child = focus;
            }
            api.Clear(&focus);
        }
        VARIANT state{};
        const bool password = SUCCEEDED((element ? element.Get() : &root)->get_accState(child, &state))
                              && state.vt == VT_I4 && (state.lVal & STATE_SYSTEM_PROTECTED);
        api.Clear(&state);
        return password;
    }

} // anonymous namespace

    bool IsPassword(const HWND window) {
        // Without the API the object WM_GETOBJECT hands out could never be claimed and would leak
        const Api& api = _api();
        if (!api.FromLresult || !api.Clear) {
            return _passwordEdit(window);
        }
        DWORD_PTR result = 0;
        if (!window || !SendMessageTimeoutW(window, WM_GETOBJECT, 0, OBJID_CLIENT,
                                            SMTO_ABORTIFHUNG, TimeoutMs, &result)) {
            return false;
        }
        if (!result) {
            return _passwordEdit(window);
        }
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool password = false;
        {
            ComPtr<IAccessible> root;
            if (SUCCEEDED(api.FromLresult(static_cast<LRESULT>(result), __uuidof(IAccessible), 0,
                                          reinterpret_cast<void**>(root.GetAddressOf()))) && root) {
                password = _protected(*root.Get(), api);
            }
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return password;
    }
}
