#pragma once

#include <memory>

#include "UserRules.hpp"

struct AppConfig;

/// The words the user taught autocorrect: the config holds them, the input thread a snapshot.
namespace Learning {

    void Register(AppConfig& config);

    /// Any thread: the words a verdict is decided with.
    std::shared_ptr<const UserRules::Compiled> Current();

    /// Input thread: saves on the UI thread.
    void Teach(WordRule rule);
}
