#pragma once

#include <windows.h>

struct AppConfig;
class Feedback;

/// Only the state a module cannot reach by including: ActionRegistry, Dispatcher and HotkeyService are free namespaces.
struct Host {
    AppConfig& Config;
    Feedback& Fb;
    HINSTANCE Instance;
};
