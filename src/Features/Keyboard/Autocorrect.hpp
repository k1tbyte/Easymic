#pragma once

#include <array>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "Convert/Detector.hpp"
#include "TypedWord.hpp"

class Feedback;
struct KeyboardSettings;
struct LearnedWords;

namespace Autocorrect {

    /// What the tracker converts with: built on Restore, immutable after, shared with the edit lane.
    struct Runtime {
        /// Every installed layout: a third one (uk next to ru) types for the pair side of its script
        std::vector<Convert::LayoutTable> Layouts;
        std::array<size_t, 2> Pair{};
        std::unique_ptr<std::array<Convert::Pack, 2>> Packs;
        Convert::Rules Rules;
        std::unordered_set<std::string> Excluded;
        uint16_t Threshold = 0;
        bool Frequency = true;
        bool Auto = false;
        bool SkipPasswords = true;
        bool SkipFullscreen = true;
        bool LogDecisions = false;

        /// The pair's table, the language its pack speaks.
        const Convert::LayoutTable& Table(const int side) const { return Layouts[Pair[side]]; }
        const Convert::LayoutTable* Find(HKL layout) const;
        /// The pair side `layout` types for, itself or by script; -1 when none.
        int SideOf(HKL layout) const;
    };

    /// Once, from Keyboard::Register: what a fix plays and says, and through whom.
    void Register(const KeyboardSettings& settings, const Feedback& feedback);

    /// UI thread. Null when the pair is not two installed layouts.
    std::shared_ptr<Runtime> Resolve(const KeyboardSettings& settings);

    /// Off the input thread: the verdict on a word typed on pair side `from`, the user's words over all.
    Convert::Verdict Decide(const Runtime& runtime, const LearnedWords& learned, std::span<const Convert::Key> word,
                            int from);

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
