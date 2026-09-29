#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

enum class MicPillMode {
    Hidden,
    Muted,
    MutedOrTalk,
};

enum class AutoCorrectMode {
    Off,
    Space,
    MidWord,
};

struct CoreSettings {
    bool Updates = true;
    bool AutoUpdate = false;
    bool SkipUac = false;
    uint16_t MultiPressWindowMs = 200;
    std::set<std::string> SkippedVersions;

    bool operator==(const CoreSettings&) const = default;
};

struct MicSettings {
    /// -1 leaves the device's level alone.
    int8_t Volume = -1;
    bool KeepVolume = true;
    int8_t BellVolume = 25;
    std::string MuteSound = "Mute";
    std::string UnmuteSound = "Unmute";
    MicPillMode Pill = MicPillMode::Muted;
    float VolumeThreshold = .001f;
    bool HideWhenInactive = true;

    bool operator==(const MicSettings&) const = default;
};

enum class WordMatch { Exact, StartsWith, Contains };

struct WordRule {
    std::string Text;
    WordMatch Match = WordMatch::Exact;
    bool CaseSensitive = false;
    bool Always = true;

    bool operator==(const WordRule&) const = default;
};

struct LearnedWords {
    std::vector<WordRule> Rules;

    bool operator==(const LearnedWords&) const = default;
};

struct KeyboardSettings {
    bool ShowLayout = false;
    std::string PairA;
    std::string PairB;
    std::string PackA;
    std::string PackB;
    AutoCorrectMode AutoCorrect = AutoCorrectMode::Off;
    bool SkipPasswords = true;
    bool SkipFullscreen = true;
    std::string Exclude;
    uint16_t Threshold = 0;
    /// Off: Punto's rules and the dictionary only, no ngram and no guards on the rules.
    bool FrequencyAnalysis = true;
    bool LogDecisions = false;
    LearnedWords Learned;
    std::string FixSound;
    uint8_t FixSoundVolume = 50;
    bool FixNotification = false;

    bool operator==(const KeyboardSettings&) const = default;
};

/// The surface, not what is drawn on it: a field belongs here if it would matter with every drawing feature deleted.
struct OverlaySettings {
    int32_t PosX = 0;
    int32_t PosY = 0;
    /// Icon edge in pixels; a pill is twice this and everything else scales off it.
    uint8_t Size = 16;
    uint8_t Opacity = 100;
    std::string FontFamily = "Segoe UI";
    bool ExcludeFromCapture = false;
    bool OnTopExclusive = false;
    bool Notifications = true;

    bool operator==(const OverlaySettings&) const = default;
};

struct TraySettings {
    std::string Provider;

    bool operator==(const TraySettings&) const = default;
};

/// The rect is the visible frame in physical pixels of the virtual screen; a zero size picks the desktop only.
struct WindowRule {
    std::string Exe;
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Width = 0;
    int32_t Height = 0;
    bool Maximized = false;

    bool operator==(const WindowRule&) const = default;
};

struct DesktopPreset {
    std::string Name;
    /// An app's k-th open window takes its k-th rule, desktops counted in order.
    std::vector<WindowRule> Windows;

    bool operator==(const DesktopPreset&) const = default;
};

struct DesktopSettings {
    std::vector<DesktopPreset> Presets;
    bool Watch = true;
    bool Follow = true;
    bool ApplyOnStartup = false;
    bool AnnounceSwitch = true;

    bool operator==(const DesktopSettings&) const = default;
};

struct HotkeyTrigger {
    /// "CTRL + SHIFT + M". A name that does not parse leaves the binding unbound but visible, not dropped.
    std::string Keys;
    uint8_t Presses = 1;
    bool OnRelease = false;
    bool Block = false;
    /// Release only: skips the action when another key was pressed meanwhile, so a key can double as a modifier.
    bool TapOnly = false;
    std::string App;

    bool operator==(const HotkeyTrigger&) const = default;
};

struct Binding {
    std::string Name;
    std::string ActionId;
    std::string Args;
    std::string Sound;
    std::string Notification;
    uint8_t SoundVolume = 100;
    bool ShowNotification = false;
    HotkeyTrigger Trigger;

    bool operator==(const Binding&) const = default;
};

struct AppConfig {
    CoreSettings Core;
    MicSettings Mic;
    KeyboardSettings Keyboard;
    OverlaySettings Overlay;
    TraySettings Tray;
    DesktopSettings Desktops;
    std::vector<Binding> Bindings;
    std::set<std::string> RecentSounds;

    bool operator==(const AppConfig&) const = default;

    void Save() const;
    /// A file that does not parse is renamed to .bad and defaults are used; a key no field has is dropped.
    static AppConfig Load();
};
