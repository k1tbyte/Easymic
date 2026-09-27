#include <cstdio>
#include <string>

#include "Core/AppConfig.hpp"
#include "Core/SettingsHost.hpp"

int main() {
    AppConfig cfg;
    const auto defaults = cfg.Keyboard;
    if (defaults.AutoCorrect || !defaults.Exclude.empty() || defaults.Threshold
        || !defaults.UseContext || defaults.LogDecisions
        || !defaults.PackA.empty() || !defaults.PackB.empty() || defaults.Learned != LearnedWords{}
        || !defaults.FixSound.empty() || defaults.FixNotification || !defaults.SkipPasswords
        || !defaults.SkipFullscreen) {
        std::puts("keyboard defaults failed");
        return 1;
    }

    Bind<&AppConfig::Keyboard, &KeyboardSettings::AutoCorrect>().Set(cfg, true);
    Bind<&AppConfig::Keyboard, &KeyboardSettings::Exclude>().Text(cfg) = "notepad.exe, chrome.exe";
    Bind<&AppConfig::Keyboard, &KeyboardSettings::Threshold>().Set(cfg, 42);
    Bind<&AppConfig::Keyboard, &KeyboardSettings::UseContext>().Set(cfg, false);
    Bind<&AppConfig::Keyboard, &KeyboardSettings::LogDecisions>().Set(cfg, true);
    cfg.Keyboard.PackA = "en.pack";
    cfg.Keyboard.PackB = "ru.pack";
    cfg.Keyboard.Learned = {.Always = {"ye"}, .Never = {"ofc", "ghbdtn"}};
    cfg.Keyboard.FixSound = "Tick";
    cfg.Keyboard.FixSoundVolume = 30;
    cfg.Keyboard.FixNotification = true;
    cfg.Keyboard.SkipFullscreen = false;

    std::string json;
    if (glz::write_json(cfg, json)) {
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
    if (glz::read_json(old, std::string{R"({"Keyboard":{"ShowLayout":true},"Version":4})"})
        || !old.Keyboard.ShowLayout || old.Keyboard.AutoCorrect || !old.Keyboard.UseContext
        || !old.Keyboard.PackA.empty() || !old.Keyboard.PackB.empty() || old.Keyboard.Learned != LearnedWords{}) {
        std::puts("keyboard missing-fields fallback failed");
        return 1;
    }
    AppConfig v4;
    if (glz::read_json(v4, std::string{R"({"Keyboard":{"PackA":"custom.pack","Exclude":"a.exe, b.exe"},"Version":4})"})
        || v4.Keyboard.PackA != "custom.pack" || v4.Keyboard.Exclude != "a.exe, b.exe"
        || !v4.Keyboard.PackB.empty()) {
        std::puts("keyboard v4 new-fields parse failed");
        return 1;
    }
    std::puts("keyboard config checks passed");
}
