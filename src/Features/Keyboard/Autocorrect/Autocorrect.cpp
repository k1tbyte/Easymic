#include "Autocorrect.hpp"

#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "Platform/Str.hpp"

namespace Autocorrect {

namespace {

    const KeyboardSettings* _settings = nullptr;
    const Feedback* _feedback = nullptr;
}

    void Register(const KeyboardSettings& settings, const Feedback& feedback) {
        _settings = &settings;
        _feedback = &feedback;
    }

    void Announce(std::wstring typed, std::wstring fixed) {
        // The worker may read the config: it is stopped while settings edits it
        Dispatcher::Post([typed = std::move(typed), fixed = std::move(fixed)] {
            _feedback->Play(_settings->FixSound, _settings->FixSoundVolume);
            if (_settings->FixNotification) {
                _feedback->Post(Str::WideToUtf8(typed + L" \u2192 " + fixed));
            }
        });
    }
}
