#include "Learning.hpp"

#include <atomic>
#include <utility>
#include <vector>

#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "definitions.h"

namespace Learning {

namespace {

    AppConfig* _config = nullptr;
    bool _suspended = false;
    std::vector<WordRule> _deferred;

    std::atomic<std::shared_ptr<const UserRules::Compiled>> _current = std::make_shared<const UserRules::Compiled>();

    void _publish() {
        _current = std::make_shared<const UserRules::Compiled>(_config->Keyboard.Learned);
    }

    void _file(WordRule rule) {
        if (_suspended) {
            _deferred.push_back(std::move(rule));
            return;
        }
        if (!UserRules::Put(_config->Keyboard.Learned, rule)) {
            return;
        }
        if (_config->Keyboard.LogDecisions) {
            Logger::Log(Logger::Level::Info, "Keyboard: learned %s %s", rule.Always ? "always" : "never", rule.Text.c_str());
        }
        _config->Save();
        _publish();
    }

} // anonymous namespace

    void Register(AppConfig& config) {
        _config = &config;
        Lifecycle::Suspend += [] { _suspended = true; };
        Lifecycle::Restore += [] {
            _suspended = false;
            for (auto& rule : std::exchange(_deferred, {})) {
                _file(std::move(rule));
            }
            _publish();
        };
    }

    std::shared_ptr<const UserRules::Compiled> Current() {
        return _current.load();
    }

    void Teach(WordRule rule) {
        Dispatcher::ToUi([rule = std::move(rule)]() mutable { _file(std::move(rule)); });
    }
}
