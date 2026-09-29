#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>

#include "AppConfig.hpp"
#include "OrderedInsert.hpp"

enum class RowKind : uint8_t {
    Check,
    Combo,
    Slider,
    SoundPicker,
    Radio,
    Text,
    Group,
    Custom,
};

struct RowField {
    int (*Get)(const AppConfig&) = nullptr;
    void (*Set)(AppConfig&, int) = nullptr;
    std::string& (*Text)(AppConfig&) = nullptr;
};

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

/// Every control is re-read from its field after any change, so a hook edits fields, never controls.
struct SettingsRow {
    RowKind Kind;
    const wchar_t* Label = L"";
    RowField Field{};
    int16_t Min = 0;
    int16_t Max = 100;
    uint8_t Step = 1;
    /// Read when the page is built, so the list can come from a registry.
    std::span<const wchar_t* const> (*Items)() = nullptr;
    void (*Changed)(HWND owner, AppConfig& cfg) = nullptr;
    /// On OK only, for what reaches outside the config file: on a click, Cancel would stop cancelling.
    void (*Commit)(HWND owner, AppConfig& cfg, const AppConfig& before) = nullptr;
    bool (*Enabled)(const AppConfig& cfg) = nullptr;
    /// Shares the previous row's line, half the control area each, with no label of its own.
    bool Beside = false;
    /// Custom only: dialog units, zero fills the page. A control created with `id` routes its click to Changed.
    uint8_t Height = 0;
    void (*Create)(HWND page, const RECT& cell, int id) = nullptr;
};

struct SettingsTab {
    const wchar_t* Title;
    std::span<const SettingsRow> Rows;
};

/// Built fresh on every visit.
struct SettingsPage {
    const wchar_t* Title;
    std::span<const SettingsRow> Rows;
    int Order = 100;
    std::span<const SettingsTab> Tabs;
};

namespace SettingsHost {

    inline constexpr int First = 0;
    inline constexpr int Last = 1000;

    inline std::vector<SettingsPage> Pages;

    inline void AddPage(const SettingsPage& page) {
        InsertByOrder(Pages, page);
    }
}
