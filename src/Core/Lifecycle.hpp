#pragma once

#include "Event.hpp"

/// What the frame tells every module about the config, on the UI thread - a module that reacts to
/// settings subscribes here rather than the frame calling it by name.
namespace Lifecycle {

    /// The settings window is taking the config over.
    inline Event<> Suspend;

    /// The config is in effect again: at startup, and whenever the settings window closes.
    inline Event<> Restore;
}
