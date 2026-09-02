//
// Round-trips AppConfig through config.json against the real header. Not part of the build -
// there is no test target yet, and one file that can be compiled by hand beats a framework
// nobody runs. From a shell with the MSVC environment loaded (build.ps1 shows how):
//
//   cl /nologo /std:c++latest /EHsc /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 ^
//      /DUNICODE /D_UNICODE /Isrc /Isrc/Core /Isrc/Platform /Isrc/UI /Ivendor/glaze/include ^
//      tools/config_roundtrip.cpp src/Core/KeyNames.cpp src/Platform/Logger.cpp
//
// It writes cfgtest.json next to itself and exits non-zero on a failure.
//
#include <cstdio>
#include "AppConfig.hpp"

static int failures = 0;

static void check(const bool ok, const char* what) {
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) { failures++; }
}

static std::string readBack() {
    std::string text;
    FILE* file = nullptr;
    fopen_s(&file, "cfgtest.json", "rb");
    if (file) {
        char buffer[8192];
        const size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[n] = '\0';
        text = buffer;
        fclose(file);
    }
    return text;
}

int main() {
    AppConfig::DefaultPath = "cfgtest.json";

    AppConfig written{};
    written.Core.Notifications = false;
    written.Core.MultiPressWindowMs = 350;
    written.Core.SkippedVersions = {"1.2.3"};
    written.Mic.Volume = 70;
    written.Mic.BellVolume = 0;
    written.Indicator.State = IndicatorState::MutedOrTalk;
    written.Indicator.Size = 24;
    written.Indicator.PosX = 1200;
    written.RecentSounds = {"Tick"};
    written.Bindings.push_back({.Name = "Mute",
                                .ActionId = "mic.toggle_mute",
                                .Notification = "Mic {mic}",
                                .ShowNotification = true,
                                .Trigger = {.Keys = "CTRL + SHIFT + M", .Presses = 2, .Block = true}});
    written.Bindings.push_back({.Name = "Say hi",
                                .ActionId = "launcher.run",
                                .Args = "cmd /c echo hi",
                                .Trigger = {.Keys = "F13"}});
    written.Save();

    const AppConfig read = AppConfig::Load();
    check(read == written, "round trip is identical");
    check(read.Version == AppConfig::CurrentVersion, "version stamped on save");
    check(read.Indicator.State == IndicatorState::MutedOrTalk, "enum survives");
    check(read.Bindings.size() == 2 && read.Bindings[1].Trigger.Keys == "F13", "nested trigger survives");

    const std::string text = readBack();
    check(text.find('\n') != std::string::npos, "written prettified");
    check(text.find("\"MutedOrTalk\"") != std::string::npos, "enum written by name");
    check(text.find("\"CTRL + SHIFT + M\"") != std::string::npos, "combination written as its name");

    // A file from another revision has to be ignored whole, not half-loaded
    AppConfig stale = written;
    stale.Version = 2;
    if (glz::write_file_json<glz::opts{.prettify = true}>(stale, std::string{"cfgtest.json"},
                                                         std::string{})) {
        failures++;
    }
    const AppConfig defaults = AppConfig::Load();
    check(defaults.Bindings.empty() && defaults.Indicator.Size == 16,
          "older revision falls back to defaults");

    // A hand-edited alias has to come back canonical, or two bindings that mean the same
    // combination stop comparing equal
    AppConfig aliased{};
    aliased.Version = AppConfig::CurrentVersion;
    aliased.Bindings.push_back({.Name = "Alias", .ActionId = "mic.toggle_mute",
                                .Trigger = {.Keys = "LCTRL + A"}});
    aliased.Bindings.push_back({.Name = "Junk", .ActionId = "mic.toggle_mute",
                                .Trigger = {.Keys = "NOPE + ZZZ"}});
    if (glz::write_file_json<glz::opts{.prettify = true}>(aliased, std::string{"cfgtest.json"},
                                                         std::string{})) {
        failures++;
    }
    const AppConfig canonical = AppConfig::Load();
    check(canonical.Bindings.size() == 2 && canonical.Bindings[0].Trigger.Keys == "CTRL + A",
          "hand-edited alias canonicalised on load");
    check(canonical.Bindings.size() == 2 && canonical.Bindings[1].Trigger.Keys == "NOPE + ZZZ",
          "unparseable name left as written");

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures;
}
