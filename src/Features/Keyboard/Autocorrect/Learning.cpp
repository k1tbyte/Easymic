#include "Learning.hpp"

#include <atomic>
#include <utility>
#include <vector>

#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace Learning {

namespace {

    AppConfig* _config = nullptr;
    bool _suspended = false;
    struct Teaching {
        std::wstring Text;
        bool Always;
        bool Prefix;
    };
    std::vector<Teaching> _deferred;

    std::atomic<std::shared_ptr<const UserRules::Compiled>> _current = std::make_shared<const UserRules::Compiled>();

    void _publish() {
        _current = std::make_shared<const UserRules::Compiled>(_config->Keyboard.Learned);
    }

    void _file(Teaching teaching) {
        if (_suspended) {
            _deferred.push_back(std::move(teaching));
            return;
        }
        const WordRule rule{.Text = Str::WideToUtf8(teaching.Text),
                            .Match = teaching.Prefix ? WordMatch::StartsWith : WordMatch::Exact, .Always = teaching.Always};
        if (UserRules::Put(_config->Keyboard.Learned, rule)) {
            if (_config->Keyboard.LogDecisions) {
                Logger::Log(Logger::Level::Info, "Keyboard: learned %s %s", teaching.Always ? "always" : "never", rule.Text.c_str());
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
            for (auto& teaching : std::exchange(_deferred, {})) {
                _file(std::move(teaching));
            }
            _publish();
        };
    }

    std::shared_ptr<const UserRules::Compiled> Current() {
        return _current.load();
    }

    void Teach(std::wstring text, const bool always, const bool prefix) {
        Dispatcher::ToUi([teaching = Teaching{std::move(text), always, prefix}]() mutable { _file(std::move(teaching)); });
    }
}
