#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppConfig.hpp"
#include "SoundCatalog.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

/**
 * @brief What an action says and plays when it fires.
 *
 * Everything here runs on the hotkey worker except Bind and AddResolver, which run at startup.
 * Nothing writes the config.
 */
class Feedback {
public:
    /// How text reaches the screen. The kernel has no business knowing which window shows it,
    /// which message carries it or where it goes, so whoever owns the window hands this in at
    /// Bind time and keeps the handle on its own side.
    using PostFn = void (*)(const std::wstring& text);
    /// Replaces a feature's own state tokens in the text.
    using ResolveFn = std::string (*)(std::string text);

private:
    const AppConfig& _cfg;
    /// Unlike a window handle, this one is used right here - SoundCatalog plays from resources.
    HINSTANCE _instance = nullptr;
    PostFn _post = nullptr;
    std::vector<ResolveFn> _resolvers;

public:
    explicit Feedback(const AppConfig& config) : _cfg(config) {}

    void Bind(HINSTANCE instance, PostFn post) {
        _instance = instance;
        _post = post;
    }

    void AddResolver(const ResolveFn resolve) {
        _resolvers.push_back(resolve);
    }

    /**
     * @brief What an action announces, as far as it can be known when the hotkey is registered.
     *
     * An empty text means the action's own default, so a config saved before that default
     * existed still gets one; the tokens that cannot differ between two presses are resolved
     * right here, which leaves the default free of braces and Expand with no work.
     */
    static std::string Compose(const std::string& text, const std::string_view fallback,
                               const std::string& name, const std::string_view keyName) {
        const std::string source = text.empty() ? std::string{fallback} : text;
        if (source.empty()) {
            return {};
        }

        auto resolved = Str::Replace(source, Tokens::Name, name);
        return resolved.find(Tokens::Key) == std::string::npos
                   ? resolved
                   : Str::Replace(std::move(resolved), Tokens::Key, keyName);
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
            Play(sound, soundVolume);
            // Empty for an action whose handler runs somewhere else - sound and text still apply
            if (handler) {
                handler();
            }
            Notify(notification);
        };
    }

    /// A SoundCatalog key or file at 0-100; empty is silence.
    void Play(const std::string& sound, const uint8_t volume) const {
        SoundCatalog::Play(_instance, sound, volume);
    }

    /**
     * @brief Resolves the state tokens through the features' resolvers.
     *
     * Runs on the hotkey worker, after the action - a state token must read what it has just
     * done. Public because an action that answers later, a captured command, has to resolve them
     * before it starts, while the state is still the one the key was pressed on.
     */
    std::string Expand(const std::string& text) const {
        if (text.find('{') == std::string::npos) {
            return text;
        }

        std::string expanded = text;
        for (const ResolveFn resolve : _resolvers) {
            expanded = resolve(std::move(expanded));
        }
        return expanded;
    }

    /// Puts text that is already resolved on the indicator, from any thread - it travels through
    /// the window's own message queue.
    void Post(const std::string& resolved) const {
        if (resolved.empty() || !_cfg.Overlay.Notifications || !_post) {
            return;
        }

        _post(Str::Utf8ToWide(resolved));
    }

    // Reading the config from the worker is safe by construction: the worker is joined before
    // the settings window opens and restarted after it closes.
    void Notify(const std::string& text) const {
        Post(Expand(text));
    }

    /**
     * @brief A poster that can outlive this object.
     *
     * What an action that answers on a thread of its own needs - a captured command can finish
     * minutes later, and by then the process may be shutting down. The poster and the switch are
     * taken by value here, so a late answer posts to a window that may be gone rather than
     * through an object that is.
     */
    std::function<void(const std::string&)> Poster() const {
        return [post = _post, enabled = _cfg.Overlay.Notifications](const std::string& resolved) {
            if (!resolved.empty() && enabled && post) {
                post(Str::Utf8ToWide(resolved));
            }
        };
    }
};

