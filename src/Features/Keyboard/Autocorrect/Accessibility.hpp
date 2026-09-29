#pragma once

#include <windows.h>

namespace Accessibility {

    /// Off the input thread: the element focused in `window` is a password field. Asks the app over MSAA with a short
    /// timeout; oleacc is delay-loaded, so an idle app never maps it.
    bool IsPassword(HWND window);
}
