#pragma once

#include <windows.h>

struct AppConfig;
class Feedback;

/**
 * @brief The only thing a feature module is handed.
 *
 * ActionRegistry, Dispatcher and HotkeyService are free namespaces a module reaches by including
 * them, so they are not carried here. What this holds is the state a module cannot reach on its
 * own, and it is the reason every module registers through one signature: void(Host&).
 */
struct Host {
    AppConfig& Config;
    Feedback& Fb;
    HINSTANCE Instance;
};
