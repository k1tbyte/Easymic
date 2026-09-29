#include "AppConfig.hpp"

#include "AppConfigJson.hpp"
#include "Hotkeys/KeyNames.hpp"
#include "Platform/File.hpp"
#include "Platform/Foreground.hpp"
#include "definitions.h"

namespace {

    const std::wstring& _path() {
        static const std::wstring path = File::NextToExe(CONFIG_NAME);
        return path;
    }

} // anonymous namespace

void AppConfig::Save() {
    for (auto& binding : Bindings) {
        binding.Trigger.App = Foreground::CanonicalApp(binding.Trigger.App);
    }
    std::string json;
    if (const auto ec = glz::write<glz::opts{.prettify = true}>(*this, json)) {
        LOG_ERROR("Config save failed: %s", glz::format_error(ec).c_str());
    } else if (!File::Write(_path().c_str(), json)) {
        LOG_ERROR("Config save failed: 0x%08lX", GetLastError());
    }
}

AppConfig AppConfig::Load() {
    AppConfig config{};
    // A missing file is the normal first run
    const auto json = File::Read(_path().c_str());
    if (!json) {
        return config;
    }
    if (const auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(config, *json)) {
        LOG_ERROR("Config load failed: %s", glz::format_error(ec, *json).c_str());
        return {};
    }

    // Whatever the file says becomes the canonical spelling, so two bindings that mean the
    // same combination compare equal - KeyNames::Parse accepts LCTRL, which Format never
    // prints, and a hand-edited file is exactly where that shows up. A name that does not
    // parse is left as the user wrote it: the binding shows up unbound rather than blank.
    for (auto& binding : config.Bindings) {
        if (const uint64_t mask = KeyNames::Parse(binding.Trigger.Keys)) {
            binding.Trigger.Keys = KeyNames::Format(mask);
        }
        binding.Trigger.App = Foreground::CanonicalApp(binding.Trigger.App);
    }
    return config;
}
