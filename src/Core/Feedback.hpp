#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppConfig.hpp"
#include "SoundCatalog.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

/// Runs on the hotkey worker except Bind and AddResolver (startup); nothing here writes the config.
class Feedback {
public:
    using PostFn = void (*)(std::wstring text);
    using ResolveFn = std::string (*)(std::string text);

private:
    const AppConfig& _cfg;
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

    /// An empty text takes the action's default; tokens that cannot differ between presses resolve here.
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

    /// Sound first, it is the instant feedback; text last, it reports.
    std::function<void()> Wrap(std::function<void()> handler, std::string sound,
                               const uint8_t soundVolume, std::string notification) const {
        if (sound.empty() && notification.empty()) {
            return handler;
        }

        return [this, handler = std::move(handler), sound = std::move(sound), soundVolume,
                notification = std::move(notification)] {
            Play(sound, soundVolume);
            if (handler) {
                handler();
            }
            Notify(notification);
        };
    }

    void Play(const std::string& sound, const uint8_t volume) const {
        SoundCatalog::Play(_instance, sound, volume);
    }

    /// Public: an action that answers later resolves its tokens before it starts, while the key's state still holds.
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

    void Post(const std::string& resolved) const {
        if (resolved.empty() || !_cfg.Overlay.Notifications || !_post) {
            return;
        }

        _post(Str::Utf8ToWide(resolved));
    }

    void Notify(const std::string& text) const {
        Post(Expand(text));
    }

    /// By value: a captured command can finish minutes later, when this object may be gone.
    std::function<void(const std::string&)> Poster() const {
        return [post = _post, enabled = _cfg.Overlay.Notifications](const std::string& resolved) {
            if (!resolved.empty() && enabled && post) {
                post(Str::Utf8ToWide(resolved));
            }
        };
    }
};
