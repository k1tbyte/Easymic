#pragma once

#include <vector>

#include "AppConfig.hpp"

class Feedback;

namespace Bindings {

    /// Registers every configured binding that can be registered and publishes the result. One that
    /// cannot (bad combination, unknown or declined action) stays in the config and never fires.
    void Apply(const std::vector<Binding>& bindings, Feedback& feedback);
}
