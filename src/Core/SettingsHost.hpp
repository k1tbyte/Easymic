#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>

#include "AppConfig.hpp"

/// Which control a row becomes.
enum class RowKind : uint8_t {
    Check,
    Combo,
    Slider,
    /// A bundled sound or a file, with a browse button - the one string-valued kind.
    SoundPicker,
    Text,
    /// A group box around the rows after it, up to the next Group.
    Group,
    /// Controls the grid places but does not understand, made by the row's own Create.
    Custom,
};

/// How a row reaches its value: Get/Set for the int-valued kinds, Text for SoundPicker.
struct RowField {
    int (*Get)(const AppConfig&) = nullptr;
    void (*Set)(AppConfig&, int) = nullptr;
    std::string& (*Text)(AppConfig&) = nullptr;
};

/// cfg.*Section.*Field as a binding - Bind<&AppConfig::Core, &CoreSettings::Updates>().
template <auto Section, auto Field>
constexpr RowField Bind() {
    using T = std::remove_cvref_t<decltype(std::declval<AppConfig&>().*Section.*Field)>;
    if constexpr (std::is_same_v<T, std::string>) {
        return {.Text = [](AppConfig& cfg) -> std::string& { return cfg.*Section.*Field; }};
    } else {
        return {.Get = [](const AppConfig& cfg) { return static_cast<int>(cfg.*Section.*Field); },
                .Set = [](AppConfig& cfg, int value) { cfg.*Section.*Field = static_cast<T>(value); }};
    }
}

/**
 * @brief One line of a settings page, described rather than built.
 *
 * Every control is re-read from its field after any change, so a hook edits other fields and
 * never other controls - the rows that show those fields repaint themselves.
 */
struct SettingsRow {
    RowKind Kind;
    const wchar_t* Label = L"";
    RowField Field{};
    /// Slider range and its Page Up/Down step.
    int16_t Min = 0;
    int16_t Max = 100;
    uint8_t Step = 1;
    std::span<const wchar_t* const> Items{};
    /// After the field is written from the control. owner parents any prompt.
    void (*Changed)(HWND owner, AppConfig& cfg) = nullptr;
    /// On OK, for what has to reach outside the config file - never on a click, or Cancel would
    /// stop meaning Cancel.
    void (*Commit)(HWND owner, AppConfig& cfg, const AppConfig& before) = nullptr;
    bool (*Enabled)(const AppConfig& cfg) = nullptr;
    /// Custom only: the cell height in dialog units and what fills it. A control given id routes
    /// its click to Changed.
    uint8_t Height = 0;
    void (*Create)(HWND page, const RECT& cell, int id) = nullptr;
};

/// One page of the settings window. The window builds it fresh on every visit.
struct SettingsPage {
    const wchar_t* Title;
    std::span<const SettingsRow> Rows;
    /// Where the page sits in the sidebar - see the keys below.
    int Order = 100;
};

/**
 * @brief Every settings page anyone has registered, in the order the sidebar shows them.
 *
 * Registration order alone would put About wherever its owner happens to sit in main, so the
 * frame's own first and last pages pin themselves with a sort key and everything else keeps the
 * order it registered in.
 */
namespace SettingsHost {

    /// A module leaves Order alone unless it has a reason to be first or last.
    inline constexpr int First = 0;
    inline constexpr int Last = 1000;

    inline std::vector<SettingsPage> Pages;

    inline void AddPage(const SettingsPage& page) {
        Pages.insert(std::ranges::upper_bound(Pages, page.Order, {}, &SettingsPage::Order), page);
    }
}
