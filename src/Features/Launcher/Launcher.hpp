#pragma once

#include "Core/Host.hpp"

/// Running something the user typed: one action whose argument is the command line.
namespace Launcher {
    void Register(Host& host);
}
