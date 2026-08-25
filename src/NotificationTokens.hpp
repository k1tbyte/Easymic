#ifndef EASYMIC_NOTIFICATIONTOKENS_HPP
#define EASYMIC_NOTIFICATIONTOKENS_HPP

#include <iterator>

/**
 * @brief The placeholders a notification text may contain.
 *
 * One table behind everything: the menu that offers them, the descriptions the user reads and the
 * literals the resolver replaces. Adding a token is a line here plus its case in the resolver.
 */
namespace NotificationTokens {

    inline constexpr char Name[]   = "{name}";
    inline constexpr char Key[]    = "{key}";
    inline constexpr char Volume[] = "{volume}";
    inline constexpr char Bell[]   = "{bell}";
    inline constexpr char Stdout[] = "{stdout}";

    struct Token {
        const char* Text;
        const char* Description;
        /// Only a custom action runs a command, so only it can print anything
        bool CustomOnly;
    };

    inline constexpr Token All[] = {
        {Name,   "Action name",                             false},
        {Key,    "Key combination",                         false},
        {Volume, "Microphone volume, 0-100",                false},
        {Bell,   "Bell sound, on or off",                   false},
        {Stdout, "Command output - waits for it to finish", true },
    };

    inline constexpr int Count = static_cast<int>(std::size(All));
}

#endif //EASYMIC_NOTIFICATIONTOKENS_HPP
