#pragma once

#include <span>
#include <vector>

/**
 * @brief The placeholders an action's text fields may contain.
 *
 * One table behind the menus that offer them and the descriptions the user reads. The kernel owns
 * {name} and {key}; a feature adds its own in Register and replaces them in its resolver.
 */
namespace Tokens {

    inline constexpr char Name[] = "{name}";
    inline constexpr char Key[]  = "{key}";

    /// Which field offers a token - they are resolved by different code at different times.
    enum Field : unsigned {
        Notification = 1u << 0,
        Command      = 1u << 1,
    };

    struct Token {
        const char* Text;
        const char* Description;
        unsigned Fields;
        /// Only a custom action runs a command, so only it can print anything or be launched
        /// somewhere. Command tokens are custom by construction; this gates the notification ones.
        bool CustomOnly;
    };

    namespace Detail {
        inline std::vector<Token>& All() {
            static std::vector<Token> all = {
                {Name, "Action name", Notification, false},
                {Key, "Key combination", Notification, false},
            };
            return all;
        }
    }

    /// From a feature's Register, on the UI thread.
    inline void Add(const Token& token) {
        Detail::All().push_back(token);
    }

    /// In the order they were added: the menus list them so.
    inline std::span<const Token> All() {
        return Detail::All();
    }
}
