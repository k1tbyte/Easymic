#include "Learning.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "Convert/Pack.hpp"
#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Input/Input.hpp"
#include "Core/Lifecycle.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace Learning {

namespace {

    // UI thread
    AppConfig* _config = nullptr;
    bool _suspended = false;
    /// Taught while settings held the config: filed once it hands it back.
    std::vector<std::pair<std::wstring, bool>> _deferred;

    // Input thread
    std::shared_ptr<const LearnedWords> _current = std::make_shared<const LearnedWords>();

    std::string _entry(const std::wstring_view text) {
        return Str::WideToUtf8(Convert::WordKey(text));
    }

    void _publish() {
        Input::Post([words = std::make_shared<const LearnedWords>(_config->Keyboard.Learned)] {
            _current = words;
        });
    }

    void _file(const std::wstring& text, const bool always) {
        if (_suspended) {
            _deferred.emplace_back(text, always);
            return;
        }
        const std::string entry = File(_config->Keyboard.Learned, text, always);
        if (!entry.empty()) {
            if (_config->Keyboard.LogDecisions) {
                Logger::Log(Logger::Level::Info, "Keyboard: learned %s %s", always ? "always" : "never", entry.c_str());
            }
            _config->Save();
            _publish();
        }
    }

} // anonymous namespace

    void Register(AppConfig& config) {
        _config = &config;
        Lifecycle::Suspend += [] { _suspended = true; };
        Lifecycle::Restore += [] {
            _suspended = false;
            for (const auto& [text, always] : std::exchange(_deferred, {})) {
                _file(text, always);
            }
            _publish();
        };
    }

    const std::shared_ptr<const LearnedWords>& Current() {
        return _current;
    }

    void Teach(std::wstring text, const bool always) {
        Dispatcher::ToUi([text = std::move(text), always] { _file(text, always); });
    }

    std::string File(LearnedWords& words, const std::wstring_view text, const bool always) {
        std::string entry = _entry(text);
        auto& into = always ? words.Always : words.Never;
        if (entry.empty() || std::ranges::contains(into, entry)) {
            return {};
        }
        std::erase(always ? words.Never : words.Always, entry);
        into.push_back(entry);
        return entry;
    }

    std::optional<bool> Answer(const LearnedWords& words, const std::wstring_view text) {
        const std::string entry = _entry(text);
        if (std::ranges::contains(words.Always, entry)) {
            return true;
        }
        if (std::ranges::contains(words.Never, entry)) {
            return false;
        }
        return std::nullopt;
    }
}
