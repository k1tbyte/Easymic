#include "Autocorrect.hpp"

#include <algorithm>

#include "Accessibility.hpp"
#include "Core/AppConfig.hpp"
#include "Learning.hpp"
#include "Platform/Str.hpp"

namespace Autocorrect {

namespace {

    Convert::Side _side(const Runtime& runtime, const int side) {
        return {&runtime.Table(side), &(*runtime.Packs)[side]};
    }
}

    Convert::Verdict Decide(const Runtime& runtime, const UserRules::Compiled& learned,
                            const std::span<const Convert::Key> word, const int from) {
        Convert::Verdict verdict = Convert::Detect(word, _side(runtime, from), _side(runtime, 1 - from),
                                                     runtime.Threshold / 100.0, &runtime.Rules, runtime.Frequency);
        if (const auto always = learned.Answer(verdict.Typed); always && !verdict.Fixed.empty()) {
            verdict.WrongLayout = *always;
            verdict.ByUser = true;
            verdict.Undecided = false;
        }
        return verdict;
    }

    Convert::Verdict Early(const Runtime& runtime, const UserRules::Compiled& learned,
                           const std::span<const Convert::Key> word, const int from) {
        Convert::Verdict verdict = Convert::Early(word, _side(runtime, from), _side(runtime, 1 - from), &runtime.Rules,
                                                  runtime.Frequency);
        verdict.WrongLayout = verdict.WrongLayout && !learned.Refused(verdict.Typed);
        return verdict;
    }

    void Overruled(const Runtime& runtime, const TypedWord::Word& word, const int side) {
        const std::span<const Convert::Key> keys{word.Keys.data(), word.Count};
        if (std::ranges::contains(keys, VK_SPACE, &Convert::Key::Vk)) {
            return;
        }
        const bool always = word.Judged != TypedWord::Judgement::Fixed;
        const bool prefix = !always && word.EarlyAt;
        // Taught as typed, and a fix is on screen on the other side
        const Convert::LayoutTable& typed = runtime.Table(always ? side : 1 - side);
        Learning::Teach({.Text = Str::WideToUtf8(typed.Render(prefix ? keys.first(word.EarlyAt) : keys)),
                         .Match = prefix ? WordMatch::StartsWith : WordMatch::Exact,
                         .Always = always});
    }

    bool Guarded(const Runtime& runtime, const HWND focus) {
        if (!runtime.SkipPasswords || !Accessibility::IsPassword(focus)) {
            return false;
        }
        LogSkip(runtime, "password field", 0);
        return true;
    }
}
