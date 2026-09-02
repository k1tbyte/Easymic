#pragma once

#include "Core/Host.hpp"

/// The keyboard layout: one action, no state of its own - Windows keeps the layout ring.
namespace Keyboard {
    void Register(Host& host);
}
