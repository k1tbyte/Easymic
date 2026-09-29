#pragma once

#include <windows.h>

namespace Accessibility {

    /// Off the input thread: the element focused in `window` is a password field. Asks the app over
    /// MSAA with a short timeout; oleacc loads on the first call.
    bool IsPassword(HWND window);
}
