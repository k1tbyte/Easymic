#include <cstdio>
#include <string>

#include "Core/AppConfigJson.hpp"
#include "Core/SettingsHost.hpp"

int main() {
    AppConfig cfg;
    const auto defaults = cfg.Keyboard;
    if (defaults.AutoCorrect != AutoCorrectMode::Off || !defaults.Exclude.empty() || defaults.Threshold
        || defaults.LogDecisions
        || !defaults.PackA.empty() || !defaults.PackB.empty() || defaults.Learned != LearnedWords{}
        || !defaults.FixSound.empty() || defaults.FixNotification || !defaults.SkipPasswords
        || !defaults.SkipFullscreen || !defaults.FrequencyAnalysis) {
        std::puts("keyboard defaults failed");
        return 1;
    }

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
    if (glz::write_json(cfg, json) || !json.contains(R"("AutoCorrect":"MidWord")")
        || !json.contains(R"("Match":"Contains")") || json.contains(R"("Always":[)")) {
        std::puts("keyboard serialization failed");
        return 1;
    }
    AppConfig loaded;
    if (glz::read_json(loaded, json) || loaded.Keyboard != cfg.Keyboard) {
        std::puts("keyboard round-trip failed");
        return 1;
    }
    if (loaded.Keyboard.PackA != "en.pack" || loaded.Keyboard.PackB != "ru.pack") {
        std::puts("keyboard pack round-trip failed");
        return 1;
    }
    AppConfig old;
    if (glz::read_json(old, std::string{R"({"Keyboard":{"ShowLayout":true}})"})
        || !old.Keyboard.ShowLayout || old.Keyboard.AutoCorrect != AutoCorrectMode::Off
        || !old.Keyboard.PackA.empty() || !old.Keyboard.PackB.empty() || old.Keyboard.Learned != LearnedWords{}) {
        std::puts("keyboard missing-fields fallback failed");
        return 1;
    }
    std::puts("keyboard config checks passed");
}
