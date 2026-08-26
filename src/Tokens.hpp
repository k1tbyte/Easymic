#ifndef EASYMIC_TOKENS_HPP
#define EASYMIC_TOKENS_HPP

#include <iterator>

/**
 * @brief The placeholders an action's text fields may contain.
 *
 * One table behind everything: the menus that offer them, the descriptions the user reads and the
 * literals the resolvers replace. Adding a token is a line here plus its case in a resolver.
 */
namespace Tokens {

    inline constexpr char Name[]   = "{name}";
    inline constexpr char Key[]    = "{key}";
    inline constexpr char Volume[] = "{volume}";
    inline constexpr char Mic[]    = "{mic}";
    inline constexpr char Bell[]   = "{bell}";
    inline constexpr char Stdout[] = "{stdout}";
    inline constexpr char Dir[]    = "{dir}";

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

    inline constexpr Token All[] = {
        {Name,   "Action name",                             Notification, false},
        {Key,    "Key combination",                         Notification, false},
        {Volume, "Microphone volume, 0-100",                Notification, false},
        {Mic,    "Microphone, on or off",                   Notification, false},
        {Bell,   "Bell sound, on or off",                   Notification, false},
        {Stdout, "Command output - waits for it to finish", Notification, true },
        {Dir,    "Folder the active Explorer tab shows",    Command,      true },
    };

    inline constexpr int Count = static_cast<int>(std::size(All));
}

#endif //EASYMIC_TOKENS_HPP
