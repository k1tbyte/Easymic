#pragma once

struct AppConfig;

/// The Desktops settings page: a tab per desktop, its name, its windows, and what to do with them.
namespace Page {
    void Register(AppConfig& config);
}
