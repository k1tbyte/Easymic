#include "ActionList.hpp"

#include <commctrl.h>
#include <string>

#include "ActionDialog.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Hotkeys/KeyNames.hpp"
#include "Platform/Windowing/Controls.hpp"
#include "Platform/Windowing/Foreground.hpp"
#include "Resources/Resource.h"
#include "Str.hpp"

namespace {

    /// A tint of the list's own background, not a fixed colour: pulling red and blue down leaves a green cast on any base.
    COLORREF _commandRowColor() {
        const COLORREF background = GetSysColor(COLOR_WINDOW);
        return RGB(GetRValue(background) * 93 / 100, GetGValue(background),
                   GetBValue(background) * 93 / 100);
    }

    bool _runsCommand(const ActionDesc* desc) {
        return desc && HasFlag(desc->Flags, ActionFlags::RunsCommand);
    }

    std::string _describe(const HotkeyTrigger& trigger) {
        std::string hotkey = trigger.Keys;
        if (hotkey.empty()) {
            return hotkey;
        }

        if (!trigger.App.empty()) {
            hotkey += " [" + trigger.App + "]";
        }
        if (trigger.Presses > 1) {
            hotkey += " x" + std::to_string(trigger.Presses);
        }
        if (trigger.OnRelease) {
            hotkey += " (release)";
        }
        return hotkey;
    }

    void _addRow(HWND list, const std::string& name, const std::string& hotkey, const std::string& command,
                 const bool runsCommand) {
        const std::wstring cells[] = {Str::Utf8ToWide(name), Str::Utf8ToWide(hotkey), Str::Utf8ToWide(command)};

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0));
        item.lParam = runsCommand; // the custom draw reads it back
        item.pszText = const_cast<LPWSTR>(cells[0].c_str());
        const int index = static_cast<int>(SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item));

        for (int column = 1; column < static_cast<int>(std::size(cells)); column++) {
            item.iSubItem = column;
            item.pszText = const_cast<LPWSTR>(cells[column].c_str());
            SendMessageW(list, LVM_SETITEMTEXTW, index, (LPARAM)&item);
        }
    }

    void _fill(HWND list, const AppConfig& cfg) {
        SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);

        for (const Binding& binding : cfg.Bindings) {
            _addRow(list, binding.Name, _describe(binding.Trigger), binding.Args,
                    _runsCommand(ActionRegistry::Find(binding.ActionId)));
        }
        _addRow(list, "+ Add action...", "", "", false);

        Controls::FitColumns(list, {32, 38});
    }

    /// Unique is the combination with its press count and app. Names compare safely: every stored one is KeyNames::Format's.
    void _clearHotkey(AppConfig& cfg, const HotkeyTrigger& wanted, const int exceptIndex) {
        if (wanted.Keys.empty()) {
            return;
        }

        for (int i = 0; i < static_cast<int>(cfg.Bindings.size()); i++) {
            HotkeyTrigger& trigger = cfg.Bindings[i].Trigger;
            if (i != exceptIndex && trigger.Keys == wanted.Keys && trigger.Presses == wanted.Presses
                && trigger.App == wanted.App) {
                trigger.Keys.clear();
            }
        }
    }

    /// An index past the end is a new binding; the seed says which action it runs.
    void _edit(HWND owner, AppConfig& cfg, const int index, const Binding& seed) {
        const bool isExisting = index < static_cast<int>(cfg.Bindings.size());
        Binding binding = isExisting ? cfg.Bindings[index] : seed;
        const ActionDesc* const desc = ActionRegistry::Find(binding.ActionId);
        const bool runsCommand = _runsCommand(desc);

        const ActionLayout layout{
            .Title = binding.Name.empty() ? "New action" : binding.Name,
            // An id no module registered still shows its argument: it cannot fire, and a labelled row the user can fix beats a hidden one
            .ArgsLabel = desc ? (runsCommand ? "" : std::string{desc->ArgsLabel})
                              : (binding.Args.empty() ? "" : "Argument"),
            .ArgsHint = desc ? std::string{desc->ArgsHint} : "",
            .RunsCommand = runsCommand,
            .HasSound = !desc || !HasFlag(desc->Flags, ActionFlags::NoSound),
            .HoldOnly = desc && desc->MakeRelease != nullptr,
            .AllowDelete = isExisting,
        };

        if (binding.Notification.empty()) {
            binding.Notification = desc ? std::string{desc->DefaultNotification}
                                        : std::string{ActionRegistry::DefaultNotification};
        }
        if (!KeyNames::Parse(binding.Trigger.Keys)) {
            binding.Trigger.Keys.clear();
        }

        switch (ActionDialog::Show(GetModuleHandleW(nullptr), owner, binding, layout, cfg.RecentSounds)) {
            case ActionDialog::Result::Cancel:
                return;
            case ActionDialog::Result::Delete:
                cfg.Bindings.erase(cfg.Bindings.begin() + index);
                return;
            case ActionDialog::Result::Save:
                break;
        }

        binding.Trigger.App = Foreground::CanonicalApp(binding.Trigger.App);
        _clearHotkey(cfg, binding.Trigger, isExisting ? index : -1);

        if (isExisting) {
            cfg.Bindings[index] = std::move(binding);
        } else {
            cfg.Bindings.push_back(std::move(binding));
        }
    }

    void _add(HWND owner, AppConfig& cfg) {
        HMENU menu = CreatePopupMenu();

        const int count = static_cast<int>(ActionRegistry::All.size());
        std::string_view group;
        for (int i = 0; i < count; i++) {
            const ActionDesc& desc = ActionRegistry::All[i];
            // Registration order is the module order in main: a group's actions are contiguous
            if (i && desc.Group != group) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            }
            group = desc.Group;
            AppendMenuW(menu, MF_STRING, i + 1, Str::Utf8ToWide(std::string{desc.Title}).c_str());
        }

        POINT cursor;
        GetCursorPos(&cursor);
        const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, owner, nullptr);
        DestroyMenu(menu);

        if (!chosen) {
            return;
        }

        const ActionDesc& desc = ActionRegistry::All[chosen - 1];
        _edit(owner, cfg, static_cast<int>(cfg.Bindings.size()),
              {.Name = std::string{desc.Title}, .ActionId = std::string{desc.Id}, .Sound = std::string{desc.DefaultSound}});
    }

    /// The list notifies its parent, which is the grid's page: so the page is subclassed while it lives.
    LRESULT CALLBACK _pageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(page, _pageProc, id);
        } else if (message == WM_NOTIFY && reinterpret_cast<const NMHDR*>(lParam)->idFrom == IDC_HOTKEYS_LIST) {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            auto& cfg = *reinterpret_cast<AppConfig*>(data);

            if (header->code == NM_DBLCLK) {
                const HWND list = header->hwndFrom;
                const int row = reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem;
                if (row != -1) {
                    const HWND owner = GetAncestor(page, GA_ROOT);
                    row < static_cast<int>(cfg.Bindings.size()) ? _edit(owner, cfg, row, {}) : _add(owner, cfg);
                    _fill(list, cfg);
                }
                return 0;
            }

            if (header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && draw->nmcd.lItemlParam) {
                    draw->clrTextBk = _commandRowColor();
                }
                return CDRF_DODEFAULT;
            }
        }

        return DefSubclassProc(page, message, wParam, lParam);
    }

} // anonymous namespace

void ActionList::Create(HWND page, const RECT& cell, AppConfig& cfg) {
    const HWND list = Controls::Create(page, page, WC_LISTVIEWW, L"",
                                       LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                                       cell, IDC_HOTKEYS_LIST, WS_EX_CLIENTEDGE);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

    const wchar_t* titles[] = {L"Action", L"Hotkey", L"Command"};
    LVCOLUMNW column{.mask = LVCF_TEXT | LVCF_SUBITEM};
    for (int i = 0; i < static_cast<int>(std::size(titles)); i++) {
        column.iSubItem = i;
        column.pszText = const_cast<wchar_t*>(titles[i]);
        SendMessageW(list, LVM_INSERTCOLUMNW, i, (LPARAM)&column);
    }

    SetWindowSubclass(page, _pageProc, 0, reinterpret_cast<DWORD_PTR>(&cfg));
    _fill(list, cfg);
}
