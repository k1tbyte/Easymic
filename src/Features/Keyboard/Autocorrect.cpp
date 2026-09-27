#include "Autocorrect.hpp"

#include <algorithm>

#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "InputLanguage.hpp"
#include "KeyboardExclusions.hpp"
#include "KeyboardPacks.hpp"
#include "Learning.hpp"
#include "Platform/Accessibility.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace Autocorrect {

namespace {

    const KeyboardSettings* _settings = nullptr;
    const Feedback* _feedback = nullptr;
}

    void Register(const KeyboardSettings& settings, const Feedback& feedback) {
        _settings = &settings;
        _feedback = &feedback;
    }

    const Convert::LayoutTable* Runtime::Find(const HKL layout) const {
        const auto it = std::ranges::find(Layouts, layout, &Convert::LayoutTable::Layout);
        return it == Layouts.end() ? nullptr : &*it;
    }

    std::shared_ptr<Runtime> Resolve(const KeyboardSettings& settings) {
        const auto layouts = InputLanguage::Installed();
        const int a = InputLanguage::Find(layouts, settings.PairA, 0);
        const int b = InputLanguage::Find(layouts, settings.PairB, 1);
        if (a < 0 || b < 0 || layouts[a].Handle == layouts[b].Handle) {
            return {};
        }
        auto runtime = std::make_shared<Runtime>();
        runtime->Layouts.reserve(layouts.size());
        for (const auto& layout : layouts) {
            runtime->Layouts.emplace_back(layout.Handle);
        }
        runtime->Pair = {static_cast<size_t>(a), static_cast<size_t>(b)};
        runtime->Threshold = settings.Threshold;
        runtime->SkipPasswords = settings.SkipPasswords;
        runtime->SkipFullscreen = settings.SkipFullscreen;
        runtime->UseContext = settings.UseContext;
        runtime->LogDecisions = settings.LogDecisions;
        for (const auto& exe : KeyboardExclusions::Parse(settings.Exclude)) {
            runtime->Excluded.insert(exe);
        }
        if (settings.AutoCorrect) {
            runtime->Packs = std::make_unique<std::array<Convert::Pack, 2>>();
            runtime->Auto = KeyboardPacks::Load((*runtime->Packs)[0], layouts[a].Handle, settings.PackA)
                            && KeyboardPacks::Load((*runtime->Packs)[1], layouts[b].Handle, settings.PackB);
            if (runtime->Auto) {
                KeyboardPacks::LoadRules(runtime->Rules);
                LOG_INFO("Keyboard: %zu rules", runtime->Rules.Count());
            } else {
                runtime->Packs.reset();
                LOG_ERROR("Keyboard: autocorrect needs both packs in packs/ next to the executable");
            }
        }
        return runtime;
    }

    Convert::Verdict Decide(const Runtime& runtime, const LearnedWords& learned,
                            const std::span<const Convert::Key> word, const int from,
                            const Convert::LanguageContext& context, const HWND focus) {
        const Convert::Side source{&runtime.Table(from), &(*runtime.Packs)[from]};
        const Convert::Side target{&runtime.Table(1 - from), &(*runtime.Packs)[1 - from]};
        Convert::Verdict verdict = Convert::Detect(word, source, target, runtime.UseContext ? &context : nullptr,
                                                   runtime.Threshold / 100.0, &runtime.Rules);
        if (const auto always = Learning::Answer(learned, verdict.Typed); always && !verdict.Fixed.empty()) {
            verdict.WrongLayout = *always;
            verdict.ByUser = true;
            verdict.Undecided = false;
        }
        // Asked only for a fix: the app answers across processes. The word stays out of the log
        if (verdict.WrongLayout && runtime.SkipPasswords && Accessibility::IsPassword(focus)) {
            if (runtime.LogDecisions) {
                Logger::Log(Logger::Level::Info, "Keyboard: fix skipped in a password field");
            }
            return {};
        }
        if (runtime.LogDecisions && !verdict.SourceLocale.empty()) {
            const std::string typed = Str::WideToUtf8(verdict.Typed);
            const std::string fixed = Str::WideToUtf8(verdict.Fixed);
            const std::string rule = verdict.ByRule ? " " + Str::WideToUtf8(verdict.Rule) : "";
            Logger::Log(Logger::Level::Info, "Keyboard: %s %s -> %s (%s%s, margin=%.2f, pref=%.2f)",
                        verdict.WrongLayout ? "FIX" : "ok", typed.c_str(), fixed.c_str(), verdict.Reason(),
                        rule.c_str(), verdict.Margin(), verdict.Preference);
        }
        return verdict;
    }

    void Announce(std::wstring typed, std::wstring fixed) {
        // The worker may read the config: it is stopped while settings edits it
        Dispatcher::Post([typed = std::move(typed), fixed = std::move(fixed)] {
            _feedback->Play(_settings->FixSound, _settings->FixSoundVolume);
            if (_settings->FixNotification) {
                _feedback->Post(Str::WideToUtf8(typed + L" \u2192 " + fixed));
            }
        });
    }
}
