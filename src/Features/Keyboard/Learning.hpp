#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct AppConfig;
struct LearnedWords;

/// The words the user taught autocorrect: the config holds them, the input thread a snapshot.
namespace Learning {

    void Register(AppConfig& config);

    /// Any thread: the words a verdict is decided with.
    std::shared_ptr<const LearnedWords> Current();

    /// Input thread: typed `text` filed on the UI thread, saved, in effect from the next Space.
    void Teach(std::wstring text, bool always);

    /// Files typed `text` under always or never, out of the other list. @return the entry, empty when nothing changed.
    std::string File(LearnedWords& words, std::wstring_view text, bool always);

    /// The user's answer on typed `text`: convert always (true), never (false), or none.
    std::optional<bool> Answer(const LearnedWords& words, std::wstring_view text);
}
