#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "AppConfig.hpp"
#include "CommandRunner.hpp"
#include "Core/HotkeyService.hpp"
#include "Core/KeyNames.hpp"
#include "MainWindow/MainWindow.hpp"
#include "SoundCatalog.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

/**
 * @brief What an action says and plays when it fires.
 *
 * Everything here runs on the hotkey worker except Bind and the two publishers, which the UI
 * thread calls. Nothing writes the config: the tokens that depend on state read values the
 * action itself published, so a press reports what it just did rather than the previous value.
 */
class Feedback {
    const AppConfig& _cfg;
    HWND _target = nullptr;
    HINSTANCE _instance = nullptr;

    /// The device only publishes a new level from its callback, so the action that changed it
    /// posts the value it just asked for - otherwise {volume} shows the previous one.
    std::atomic<uint8_t> _volumePercent = 0;
    std::atomic<bool> _micMuted = false;
    std::atomic<bool> _bellEnabled = true;

public:
    explicit Feedback(const AppConfig& config) : _cfg(config) {}

    void Bind(HWND target, HINSTANCE instance) {
        _target = target;
        _instance = instance;
    }

    uint8_t VolumePercent() const { return _volumePercent.load(); }
    void PublishVolumePercent(const uint8_t percent) { _volumePercent = percent; }

    bool MicMuted() const { return _micMuted.load(); }
    void PublishMicMuted(const bool muted) { _micMuted = muted; }

    bool BellEnabled() const { return _bellEnabled.load(); }
    void PublishBellEnabled(const bool enabled) { _bellEnabled = enabled; }

    /**
     * @brief What an action announces, as far as it can be known when the hotkey is registered.
     *
     * An empty text means the table default, so a config saved before that default existed still
     * gets one; the tokens that cannot differ between two presses are resolved right here, which
     * leaves the default free of braces and ExpandState with no work.
     */
    static std::string Compose(const std::string& text, const char* fallback,
                               const std::string& name, const uint64_t hotkey) {
        const std::string source = text.empty() ? fallback : text;
        if (source.empty()) {
            return {};
        }

        auto resolved = Str::Replace(source, Tokens::Name, name);
        return resolved.find(Tokens::Key) == std::string::npos
                   ? resolved
                   : Str::Replace(std::move(resolved), Tokens::Key,
                                  KeyNames::Format(hotkey));
    }

    /// Wraps an action so it announces itself. Runs on the hotkey worker, never in the hook.
    /// The sound comes first because it is the instant feedback, the text last because it reports.
    std::function<void()> Wrap(std::function<void()> handler, std::string sound,
                               const uint8_t soundVolume, std::string notification) const {
        if (sound.empty() && notification.empty()) {
            return handler;
        }

        return [this, handler = std::move(handler), sound = std::move(sound), soundVolume,
                notification = std::move(notification)] {
            if (!sound.empty()) {
                SoundCatalog::Play(_instance, sound, soundVolume);
            }
            handler();
            Show(notification);
        };
    }

    /**
     * @brief Turns a custom action into the call that launches it.
     *
     * {stdout} in the text is what asks for the output, so it also picks how the command is run.
     * The foreground is read here, on the worker, because {dir} in the command means the window
     * the user was looking at when they pressed the key - and not the one we are about to open.
     */
    std::function<void()> ForCommand(const std::string& command, const std::string& text) const {
        if (!text.contains(Tokens::Stdout)) {
            return [command] { CommandRunner::Run(command, GetForegroundWindow()); };
        }

        return [this, command, text] {
            // Resolved before launching: only this object can read the state tokens, and it is
            // not safe to touch from the command thread that answers later
            const std::string resolved = _cfg.NotificationsEnabled ? ExpandState(text) : std::string{};

            CommandRunner::RunCaptured(command, GetForegroundWindow(),
                [target = _target, resolved](const std::string& output) {
                    MainWindow::PostNotification(target,
                        Str::Utf8ToWide(Str::Replace(resolved, Tokens::Stdout, output)));
                });
        };
    }

private:
    /// Runs on the hotkey worker, after the action - a state token must read what it has just done.
    std::string ExpandState(const std::string& text) const {
        if (text.find('{') == std::string::npos) {
            return text;
        }

        auto expanded = Str::Replace(text, Tokens::Volume,
                                     std::to_string(_volumePercent.load()));
        expanded = Str::Replace(std::move(expanded), Tokens::Mic, _micMuted ? "off" : "on");
        return Str::Replace(std::move(expanded), Tokens::Bell,
                            _bellEnabled ? "on" : "off");
    }

    // Reading the config from the worker is safe by construction: the settings window disposes
    // every hotkey before it can be edited, and Dispose joins this thread.
    void Show(const std::string& text) const {
        if (text.empty() || !_cfg.NotificationsEnabled) {
            return;
        }

        MainWindow::PostNotification(_target, Str::Utf8ToWide(ExpandState(text)));
    }
};

