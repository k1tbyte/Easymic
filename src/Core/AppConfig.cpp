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

void AppConfig::Save() const {
    std::string json;
    if (const auto ec = glz::write<glz::opts{.prettify = true}>(*this, json)) {
        LOG_ERROR("Config save failed: %s", glz::format_error(ec).c_str());
    } else if (!File::Write(_path().c_str(), json)) {
        LOG_ERROR("Config save failed: 0x%08lX", GetLastError());
    }
}

AppConfig AppConfig::Load() {
    AppConfig config{};
    const auto json = File::Read(_path().c_str());
    if (!json) {
        return config;
    }
    if (const auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(config, *json)) {
        LOG_ERROR("Config load failed: %s", glz::format_error(ec, *json).c_str());
        MoveFileExW(_path().c_str(), (_path() + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING);
        return {};
    }

    // Parse accepts spellings Format never prints (LCTRL): canonicalize so hand-edited bindings compare equal
    for (auto& binding : config.Bindings) {
        if (const uint64_t mask = KeyNames::Parse(binding.Trigger.Keys)) {
            binding.Trigger.Keys = KeyNames::Format(mask);
        }
        binding.Trigger.App = Foreground::CanonicalApp(binding.Trigger.App);
    }
    return config;
}
