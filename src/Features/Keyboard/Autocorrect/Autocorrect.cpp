#include "Autocorrect.hpp"

#include <algorithm>

#include "Core/AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Feedback.hpp"
#include "../InputLanguage.hpp"
#include "../KeyboardExclusions.hpp"
#include "../KeyboardPacks.hpp"
#include "Accessibility.hpp"
#include "Learning.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace Autocorrect {

namespace {

    const KeyboardSettings* _settings = nullptr;
    const Feedback* _feedback = nullptr;

    Convert::Side _side(const Runtime& runtime, const int side) {
        return {&runtime.Table(side), &(*runtime.Packs)[side]};
    }
}

    void Register(const KeyboardSettings& settings, const Feedback& feedback) {
        _settings = &settings;
        _feedback = &feedback;
    }

    const Convert::LayoutTable* Runtime::Find(const HKL layout) const {
        const auto it = std::ranges::find(Layouts, layout, &Convert::LayoutTable::Layout);
        return it == Layouts.end() ? nullptr : &*it;
    }

    int Runtime::SideOf(const HKL layout) const {
        for (const int side : {0, 1}) {
            if (Table(side).Layout() == layout) {
                return side;
            }
        }
        const Convert::LayoutTable* table = Find(layout);
        if (!table) {
            return -1;
        }
        const bool a = table->Script() == Table(0).Script();
        const bool b = table->Script() == Table(1).Script();
        return a == b ? -1 : a ? 0 : 1;
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
        runtime->Frequency = settings.FrequencyAnalysis;
        runtime->SkipPasswords = settings.SkipPasswords;
        runtime->SkipFullscreen = settings.SkipFullscreen;
        runtime->LogDecisions = settings.LogDecisions;
        for (const auto& exe : KeyboardExclusions::Parse(settings.Exclude)) {
            runtime->Excluded.insert(exe);
        }
        runtime->MidWord = settings.AutoCorrect == AutoCorrectMode::MidWord;
        if (settings.AutoCorrect != AutoCorrectMode::Off) {
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
                            const std::span<const Convert::Key> word, const int from) {
        Convert::Verdict verdict = Convert::Detect(word, _side(runtime, from), _side(runtime, 1 - from),
                                                     runtime.Threshold / 100.0, &runtime.Rules, runtime.Frequency);
        if (const auto always = Learning::Answer(learned, verdict.Typed); always && !verdict.Fixed.empty()) {
            verdict.WrongLayout = *always;
            verdict.ByUser = true;
            verdict.Undecided = false;
        }
        return verdict;
    }

    Convert::Verdict Early(const Runtime& runtime, const LearnedWords& learned,
                           const std::span<const Convert::Key> word, const int from) {
        Convert::Verdict verdict = Convert::Early(word, _side(runtime, from), _side(runtime, 1 - from), &runtime.Rules,
                                                  runtime.Frequency);
        verdict.WrongLayout = verdict.WrongLayout && !Learning::Refused(learned, verdict.Typed);
        return verdict;
    }

    void Overruled(const Runtime& runtime, const TypedWord::Word& word, const int side) {
        const std::span<const Convert::Key> keys{word.Keys.data(), word.Count};
        if (std::ranges::contains(keys, VK_SPACE, &Convert::Key::Vk)) {
            return;
        }
        const bool always = word.Judged != TypedWord::Judgement::Fixed;
        // Taught as typed, and a fix is on screen on the other side
        const Convert::LayoutTable& typed = runtime.Table(always ? side : 1 - side);
        if (!always && word.EarlyAt) {
            Learning::Teach(typed.Render(keys.first(word.EarlyAt)) + L'*', false);
        } else {
            Learning::Teach(typed.Render(keys), always);
        }
    }

    bool Guarded(const Runtime& runtime, const HWND focus) {
        if (!runtime.SkipPasswords || !Accessibility::IsPassword(focus)) {
            return false;
        }
        // The word stays out of the log
        if (runtime.LogDecisions) {
            Logger::Log(Logger::Level::Info, "Keyboard: fix skipped in a password field");
        }
        return true;
    }

    void Log(const Runtime& runtime, const Convert::Verdict& verdict) {
        if (!runtime.LogDecisions || verdict.SourceLocale.empty()) {
            return;
        }
        Dispatcher::Post([typed = Str::WideToUtf8(verdict.Typed), fixed = Str::WideToUtf8(verdict.Fixed),
                          rule = verdict.ByRule ? " " + Str::WideToUtf8(verdict.Rule) : "",
                          fix = verdict.WrongLayout, early = verdict.Early ? " early" : "", reason = verdict.Reason(),
                          margin = verdict.Margin()] {
            Logger::Log(Logger::Level::Info, "Keyboard: %s%s %s -> %s (%s%s, margin=%.2f)", fix ? "FIX" : "ok", early,
                        typed.c_str(), fixed.c_str(), reason, rule.c_str(), margin);
        });
    }

    void LogSkip(const Runtime& runtime, const char* why, const uint64_t detail) {
        if (runtime.LogDecisions) {
            Dispatcher::Post([why, detail] {
                Logger::Log(Logger::Level::Info, "Keyboard: Space skipped, %s (%llx)", why, detail);
            });
        }
    }

    void LogFix(const Runtime& runtime, const Convert::Verdict& verdict, const bool landed, const uint64_t spaceAt) {
        if (runtime.LogDecisions && verdict.WrongLayout) {
            Dispatcher::Post([landed, ms = GetTickCount64() - spaceAt] {
                Logger::Log(Logger::Level::Info, "Keyboard: fix %s after %llu ms", landed ? "landed" : "dropped", ms);
            });
        }
    }

    void LogRun(const Runtime& runtime, const TypedWord::Word& run) {
        const Convert::LayoutTable* table = runtime.Find(run.Layout);
        if (runtime.LogDecisions && table) {
            Dispatcher::Post([text = Str::WideToUtf8(table->Render({run.Keys.data(), run.Count}))] {
                Logger::Log(Logger::Level::Info, "Keyboard: FIX %s too, the next word decided", text.c_str());
            });
        }
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
