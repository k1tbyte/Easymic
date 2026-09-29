#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

/// When the microphone's pill is on the overlay: never, while muted, or while muted or talking.
enum class MicPillMode {
    Hidden,
    Muted,
    MutedOrTalk,
};

/// When autocorrect converts a word typed in the wrong layout: never (the hotkey still does), on its Space, or
/// mid-word as soon as its start is sure.
enum class AutoCorrectMode {
    Off,
    Space,
    MidWord,
};

/// Everything that is nobody's feature in particular.
struct CoreSettings {
    bool Updates = true;
    bool AutoUpdate = false;
    bool SkipUac = false;
    /// How long a combination waits for another press before it decides how many there were.
    uint16_t MultiPressWindowMs = 200;
    std::set<std::string> SkippedVersions;

    bool operator==(const CoreSettings&) const = default;
};

struct MicSettings {
    /// The level to hold the device at, -1 to leave whatever it has alone.
    int8_t Volume = -1;
    bool KeepVolume = true;
    /// The mic state chime only - an action's sound carries its own level.
    int8_t BellVolume = 25;
    /// The chime itself, which plays whenever the device changes state - including a mute from
    /// Discord or the mixer. SoundCatalog keys or file paths.
    std::string MuteSound = "Mute";
    std::string UnmuteSound = "Unmute";
    MicPillMode Pill = MicPillMode::Muted;
    /// How loud the mic has to be before the pill counts it as talking, 0-1.
    float VolumeThreshold = .001f;
    /// Nothing is recording, so there is nothing to say about the microphone.
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

/// Words the user taught autocorrect: the config holds them, the input thread a snapshot.
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
    /// An automatic fix asks the focused field first and leaves a password alone.
    bool SkipPasswords = true;
    /// A window covering its monitor (a game) counts as excluded.
    bool SkipFullscreen = true;
    std::string Exclude;
    uint16_t Threshold = 0;
    /// Off: Punto's rules and the dictionary only, no ngram and no guards on the rules.
    bool FrequencyAnalysis = true;
    bool LogDecisions = false;
    LearnedWords Learned;
    /// What an automatic fix plays and says once it landed: a SoundCatalog key or file, empty for none.
    std::string FixSound;
    uint8_t FixSoundVolume = 50;
    bool FixNotification = false;

    bool operator==(const KeyboardSettings&) const = default;
};

/// The surface, not what is drawn on it: a field belongs here if it would still matter with
/// every feature that draws deleted.
struct OverlaySettings {
    int32_t PosX = 0;
    int32_t PosY = 0;
    /// Icon edge in pixels; a pill is twice this, and everything else scales off it.
    uint8_t Size = 16;
    /// Of the whole strip, in percent.
    uint8_t Opacity = 100;
    std::string FontFamily = "Segoe UI";
    bool ExcludeFromCapture = false;
    bool OnTopExclusive = false;
    /// Master switch for the on-screen action notifications; per binding, an empty text is the off.
    bool Notifications = true;

    bool operator==(const OverlaySettings&) const = default;
};

struct TraySettings {
    /// TrayProvider::Id of whoever paints the icon. Empty until the user picks - see Tray::Owner.
    std::string Provider;

    bool operator==(const TraySettings&) const = default;
};

/// Where one window of an app goes. The rect is the visible frame in physical pixels of the
/// virtual screen; a zero size picks the desktop only and leaves the geometry alone.
struct WindowRule {
    /// Image name, lowercase - "chrome.exe".
    std::string Exe;
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Width = 0;
    int32_t Height = 0;
    bool Maximized = false;

    bool operator==(const WindowRule&) const = default;
};

struct DesktopPreset {
    /// Given to the real desktop. Empty leaves its name alone.
    std::string Name;
    /// An app's k-th open window takes its k-th rule, desktops counted in order.
    std::vector<WindowRule> Windows;

    bool operator==(const DesktopPreset&) const = default;
};

struct DesktopSettings {
    /// One per virtual desktop, by position.
    std::vector<DesktopPreset> Presets;
    /// Places windows as they open, not only when asked to.
    bool Watch = true;
    /// Goes along with a window the user just opened when it is sent to another desktop.
    bool Follow = true;
    bool ApplyOnStartup = false;
    /// The new desktop's name on the overlay when the desktop changes, whoever changed it.
    bool AnnounceSwitch = true;

    bool operator==(const DesktopSettings&) const = default;
};

/**
 * @brief What sets a binding off.
 *
 * Nested rather than flattened into the binding: hotkeys are one trigger source, and a window
 * opening or a timer firing would be another. The nesting costs a level now and keeps the shape
 * on disk intact when one appears.
 */
struct HotkeyTrigger {
    /// The combination as it is shown, "CTRL + SHIFT + M". A name that does not parse leaves the
    /// binding unbound and visible in settings, rather than dropping it.
    std::string Keys;
    /// How many times the combination is pressed in a row to run this. One combination can drive
    /// several bindings as long as the count differs.
    uint8_t Presses = 1;
    bool OnRelease = false;
    /// Swallows the combination instead of passing it on, so the app underneath never sees it.
    bool Block = false;
    /// Release only: skips the action when another key was pressed while this one was held, which
    /// is what lets the same key double as a modifier.
    bool TapOnly = false;
    /// Optional exe basename (e.g. "chrome.exe"). When non-empty the binding fires only while
    /// that application is in the foreground. Stored canonical (lowercase basename).
    std::string App;

    bool operator==(const HotkeyTrigger&) const = default;
};

/**
 * @brief One thing a trigger does.
 *
 * A binding names an action rather than being one, so the same action can sit in the list as many
 * times as the user has keys for it - three language switches over different locale rings is the
 * case that asked for this.
 */
struct Binding {
    std::string Name;
    /// ActionDesc::Id, which is what the registry resolves to a factory.
    std::string ActionId;
    /// What the action was configured with, read its own way by each one - a comma separated list
    /// of locales for the language switch, a command line for launcher.run.
    std::string Args;
    /// SoundCatalog key or a file path, empty means silent.
    std::string Sound;
    /// Overlay text shown when the action fires, {token} aware. Empty falls back to the action's
    /// own default.
    std::string Notification;
    /// How loud this binding plays its sound, 0-100. Independent of MicSettings::BellVolume,
    /// which belongs to the mic state chime alone.
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
    /// Every binding in the order the settings list shows them.
    std::vector<Binding> Bindings;
    /// One list behind every sound picker, whatever the picker is for.
    std::set<std::string> RecentSounds;

    bool operator==(const AppConfig&) const = default;

    /// Writes the whole file.
    void Save();
    /// Reads the file, or hands back defaults. Nothing migrates: a file that does not parse is
    /// ignored whole, and a key no field has is dropped.
    static AppConfig Load();
};
