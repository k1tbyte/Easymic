#include "Autocorrect.hpp"

#include <algorithm>

#include "Core/AppConfig.hpp"
#include "../InputLanguage.hpp"
#include "../KeyboardExclusions.hpp"
#include "../KeyboardPacks.hpp"
#include "definitions.h"

namespace Autocorrect {

namespace {

    void _loadPacks(Runtime& runtime, const KeyboardSettings& settings, const HKL a, const HKL b) {
        runtime.Packs = std::make_unique<std::array<Convert::Pack, 2>>();
        auto& packs = *runtime.Packs;
        runtime.Auto = KeyboardPacks::Load(packs[0], a, settings.PackA) && KeyboardPacks::Load(packs[1], b, settings.PackB);
        if (!runtime.Auto) {
            runtime.Packs.reset();
            LOG_ERROR("Keyboard: autocorrect needs both packs in packs/ next to the executable");
            return;
        }
        KeyboardPacks::LoadRules(runtime.Rules);
        LOG_INFO("Keyboard: %zu rules", runtime.Rules.Count());
    }
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
        runtime->Excluded = KeyboardExclusions::Parse(settings.Exclude);
        runtime->MidWord = settings.AutoCorrect == AutoCorrectMode::MidWord;
        if (settings.AutoCorrect != AutoCorrectMode::Off) {
            _loadPacks(*runtime, settings, layouts[a].Handle, layouts[b].Handle);
        }
        return runtime;
    }
}
