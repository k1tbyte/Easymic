#pragma once

#include <array>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "Convert/Detector.hpp"

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
        bool Auto = false;
        bool SkipPasswords = true;
        bool SkipFullscreen = true;
        bool UseContext = true;
        bool LogDecisions = false;

        /// The pair's table, the language its pack speaks.
        const Convert::LayoutTable& Table(const int side) const { return Layouts[Pair[side]]; }
        const Convert::LayoutTable* Find(HKL layout) const;
    };

    /// Once, from Keyboard::Register: what a fix plays and says, and through whom.
    void Register(const KeyboardSettings& settings, const Feedback& feedback);

    /// UI thread. Null when the pair is not two installed layouts.
    std::shared_ptr<Runtime> Resolve(const KeyboardSettings& settings);

    /// Edit lane: the verdict on a word typed on pair side `from`, the user's words over all, logged when
    /// asked. A fix in a password field of `focus` becomes an empty verdict.
    Convert::Verdict Decide(const Runtime& runtime, const LearnedWords& learned, std::span<const Convert::Key> word,
                            int from, const Convert::LanguageContext& context, HWND focus);

    /// Input thread: an automatic fix landed; its sound and notification go to the action worker.
    void Announce(std::wstring typed, std::wstring fixed);
}
