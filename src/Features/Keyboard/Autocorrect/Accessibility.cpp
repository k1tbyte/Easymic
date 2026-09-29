#include "Accessibility.hpp"

#include <cwchar>
#include <oleacc.h>

#include "definitions.h"

namespace Accessibility {

namespace {

    /// A hung or slow app must not hold the edit lane: past this the field counts as not a password
    constexpr UINT TimeoutMs = 50;

    /// A plain Win32 edit answers no WM_GETOBJECT; its style says it.
    bool _passwordEdit(const HWND window) {
        wchar_t name[64]{};
        CharLowerBuffW(name, static_cast<DWORD>(GetClassNameW(window, name, 64)));
        return std::wcsstr(name, L"edit") && (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD);
    }

    bool _protected(IAccessible& root) {
        VARIANT child{};
        child.vt = VT_I4;
        child.lVal = CHILDID_SELF;
        ComPtr<IAccessible> element;
        VARIANT focus{};
        if (SUCCEEDED(root.get_accFocus(&focus))) {
            if (focus.vt == VT_DISPATCH && focus.pdispVal) {
                ComPtr<IDispatch> dispatch;
                dispatch.Attach(focus.pdispVal);
                dispatch.As(&element);
            } else if (focus.vt == VT_I4) {
                child = focus;
            }
        }
        VARIANT state{};
        return SUCCEEDED((element ? element.Get() : &root)->get_accState(child, &state))
               && state.vt == VT_I4 && (state.lVal & STATE_SYSTEM_PROTECTED);
    }

} // anonymous namespace

    bool IsPassword(const HWND window) {
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
            if (SUCCEEDED(ObjectFromLresult(static_cast<LRESULT>(result), __uuidof(IAccessible), 0,
                                            reinterpret_cast<void**>(root.GetAddressOf()))) && root) {
                password = _protected(*root.Get());
            }
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return password;
    }
}
