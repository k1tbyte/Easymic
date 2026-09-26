#pragma once

#include "Core/Host.hpp"

/// The keyboard layout: the switch action, the layout pill, and converting a word typed in the
/// wrong layout. Windows keeps the ring itself.
namespace Keyboard {
    void Register(Host& host);
}
