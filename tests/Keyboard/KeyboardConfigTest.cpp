#include <cstdio>
#include <string>

#include "../Check.hpp"
#include "Core/AppConfigJson.hpp"
#include "Core/SettingsHost.hpp"

int main() {
    using Test::Check;

    AppConfig cfg;
    const auto defaults = cfg.Keyboard;
    Check(defaults.AutoCorrect == AutoCorrectMode::Off && defaults.Exclude.empty() && !defaults.Threshold
              && !defaults.LogDecisions && defaults.PackA.empty() && defaults.PackB.empty()
              && defaults.Learned == LearnedWords{} && defaults.FixSound.empty() && !defaults.FixNotification,
          "keyboard defaults are off and empty");
    Check(defaults.SkipPasswords && defaults.SkipFullscreen && defaults.FrequencyAnalysis,
          "keyboard guards are on by default");

    Bind<&AppConfig::Keyboard, &KeyboardSettings::AutoCorrect>().Set(cfg, 2);
    Bind<&AppConfig::Keyboard, &KeyboardSettings::Exclude>().Text(cfg) = "notepad.exe, chrome.exe";
    Bind<&AppConfig::Keyboard, &KeyboardSettings::Threshold>().Set(cfg, 42);
    Bind<&AppConfig::Keyboard, &KeyboardSettings::LogDecisions>().Set(cfg, true);
    cfg.Keyboard.PackA = "en.pack";
    cfg.Keyboard.PackB = "ru.pack";
    cfg.Keyboard.Learned.Rules = {{.Text = ",he["}, {.Text = "ofc", .Always = false},
                                  {.Text = "Gh", .Match = WordMatch::Contains, .CaseSensitive = true, .Always = false}};
    cfg.Keyboard.FixSound = "Tick";
    cfg.Keyboard.FixSoundVolume = 30;
    cfg.Keyboard.FixNotification = true;
    cfg.Keyboard.SkipFullscreen = false;
    cfg.Keyboard.FrequencyAnalysis = false;

    std::string json;
    const bool written = !glz::write_json(cfg, json);
    Check(written && json.contains(R"("AutoCorrect":"MidWord")") && json.contains(R"("Match":"Contains")")
              && !json.contains(R"("Always":[)"),
          "keyboard serialization");

    AppConfig loaded;
    Check(!glz::read_json(loaded, json) && loaded.Keyboard == cfg.Keyboard, "keyboard round-trip");

    AppConfig old;
    Check(!glz::read_json(old, std::string{R"({"Keyboard":{"ShowLayout":true}})"}) && old.Keyboard.ShowLayout
              && old.Keyboard.AutoCorrect == AutoCorrectMode::Off && old.Keyboard.PackA.empty()
              && old.Keyboard.PackB.empty() && old.Keyboard.Learned == LearnedWords{},
          "keyboard missing-fields fallback");

    if (Test::Failures) {
        return 1;
    }
    std::puts("keyboard config checks passed");
}
