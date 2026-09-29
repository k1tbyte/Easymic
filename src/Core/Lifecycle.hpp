#pragma once

#include "Event.hpp"

/// UI thread. Restore also fires at startup, not only when the settings window closes.
namespace Lifecycle {

    inline Event<> Suspend;

    inline Event<> Restore;
}
