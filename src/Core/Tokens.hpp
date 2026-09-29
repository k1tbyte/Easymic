#pragma once

#include <span>
#include <vector>

namespace Tokens {

    inline constexpr char Name[] = "{name}";
    inline constexpr char Key[]  = "{key}";

    enum Field : unsigned {
        Notification = 1u << 0,
        Command      = 1u << 1,
    };

    struct Token {
        const char* Text;
        const char* Description;
        unsigned Fields;
        /// Only a custom action runs a command, so only it can print or launch: this gates the notification tokens.
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

    /// UI thread, from a feature's Register.
    inline void Add(const Token& token) {
        Detail::All().push_back(token);
    }

    inline std::span<const Token> All() {
        return Detail::All();
    }
}
