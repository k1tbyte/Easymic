#pragma once

#include <array>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "../Convert/Detector.hpp"
#include "TypedWord.hpp"
#include "UserRules.hpp"

class Feedback;
struct KeyboardSettings;

namespace Autocorrect {

    /// What the tracker converts with: built on Restore, immutable after, shared with the edit lane.
    struct Runtime {
        /// Every installed layout: a third one (uk next to ru) only converts by hand
        std::vector<Convert::LayoutTable> Layouts;
        std::array<size_t, 2> Pair{};
        std::unique_ptr<std::array<Convert::Pack, 2>> Packs;
        Convert::Rules Rules;
        std::unordered_set<std::string> Excluded;
        uint16_t Threshold = 0;
        bool Frequency = true;
        bool Auto = false;
        bool MidWord = false;
        bool SkipPasswords = true;
        bool SkipFullscreen = true;
        bool LogDecisions = false;

        /// The pair's table, the language its pack speaks.
        const Convert::LayoutTable& Table(const int side) const { return Layouts[Pair[side]]; }
        const Convert::LayoutTable* Find(HKL layout) const;
        /// The pair side `layout` types for, itself or by script; -1 when none.
        int SideOf(HKL layout) const;
        /// Only the pair's own layouts are judged: a pack read uk as ru, і as ы, and fixed every such word.
        bool Reads(HKL layout) const { return Table(0).Layout() == layout || Table(1).Layout() == layout; }
    };

    /// Once, from Keyboard::Register: what a fix plays and says, and through whom.
    void Register(const KeyboardSettings& settings, const Feedback& feedback);

    /// UI thread. Null when the pair is not two installed layouts.
    std::shared_ptr<Runtime> Resolve(const KeyboardSettings& settings);

    /// Off the input thread: the verdict on a word typed on pair side `from`, the user's words over all.
    Convert::Verdict Decide(const Runtime& runtime, const UserRules::Compiled& learned, std::span<const Convert::Key> word,
                            int from);

    /// Off the input thread: whether the word so far switches now, unless the user refused its start.
    Convert::Verdict Early(const Runtime& runtime, const UserRules::Compiled& learned, std::span<const Convert::Key> word,
                           int from);

    /// Input thread: the user overruled autocorrect on `word`, on screen on pair side `side` - a fix teaches never
    /// (a mid-word one its start), a keep always. A phrase teaches nothing: which of its words erred is unknown.
    void Overruled(const Runtime& runtime, const TypedWord::Word& word, int side);

    /// Edit lane, for a fix only: `focus` is a password field the fix must leave alone. Asks across processes.
    bool Guarded(const Runtime& runtime, HWND focus);

    // Any thread: a line in the log when asked, written by the action worker so the hook never waits on the file
    void Log(const Runtime& runtime, const Convert::Verdict& verdict);
    void LogSkip(const Runtime& runtime, const char* why, uint64_t detail);
    /// A fix held the typing from its Space until it landed or was dropped (a hold that timed out drops it).
    void LogFix(const Runtime& runtime, const Convert::Verdict& verdict, bool landed, uint64_t spaceAt);
    /// The undecided words a fix carried along.
    void LogRun(const Runtime& runtime, const TypedWord::Word& run);

    /// Input thread: an automatic fix landed; its sound and notification go to the action worker.
    void Announce(std::wstring typed, std::wstring fixed);
}
