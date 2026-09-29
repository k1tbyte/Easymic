#include "ActionList.hpp"

#include <commctrl.h>
#include <string>

#include "ActionDialog.hpp"
#include "SettingsRows.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Hotkeys/KeyNames.hpp"
#include "Platform/Controls.hpp"
#include "Platform/Foreground.hpp"
#include "Resources/Resource.h"
#include "Str.hpp"

namespace {

    /// Marks the row type, so it has to stay a tint of whatever the list actually paints rather
    /// than a colour of its own - pulling red and blue down leaves a green cast on any base.
    COLORREF _commandRowColor() {
        const COLORREF background = GetSysColor(COLOR_WINDOW);
        return RGB(GetRValue(background) * 93 / 100, GetGValue(background),
                   GetBValue(background) * 93 / 100);
    }

    bool _runsCommand(const ActionDesc* desc) {
        return desc && HasFlag(desc->Flags, ActionFlags::RunsCommand);
    }

    /// The trigger already stores the display name, so there is nothing to format here
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
        item.lParam = runsCommand; // the custom draw reads it back - no parallel array to keep in sync
        item.pszText = const_cast<LPWSTR>(cells[0].c_str());
        const int index = static_cast<int>(SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item));

        for (int column = 1; column < static_cast<int>(std::size(cells)); column++) {
            item.iSubItem = column;
            item.pszText = const_cast<LPWSTR>(cells[column].c_str());
            SendMessageW(list, LVM_SETITEMTEXTW, index, (LPARAM)&item);
        }
    }

    /// One row per binding in config order, then the row that adds another.
    void _fill(HWND list, const AppConfig& cfg) {
        SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);

        for (const Binding& binding : cfg.Bindings) {
            // The last column is the action's own argument, whatever that action reads it as
            _addRow(list, binding.Name, _describe(binding.Trigger), binding.Args,
                    _runsCommand(ActionRegistry::Find(binding.ActionId)));
        }
        _addRow(list, "+ Add action...", "", "", false);

        // Long commands ellipsize, the hotkey column must not
        Controls::FitColumns(list, {32, 38});
    }

    /**
     * @brief Frees a combination from every other binding.
     *
     * What has to be unique is the pair: the same combination may drive several bindings as long as
     * each wants a different number of presses. Comparing the names rather than the masks is safe
     * because every stored name came out of KeyNames::Format, which is canonical.
     */
    void _clearHotkey(AppConfig& cfg, const std::string& keys, const uint8_t presses, const std::string& app,
                      const int exceptIndex) {
        if (keys.empty()) {
            return;
        }

        for (int i = 0; i < static_cast<int>(cfg.Bindings.size()); i++) {
            HotkeyTrigger& trigger = cfg.Bindings[i].Trigger;
            if (i != exceptIndex && trigger.Keys == keys && trigger.Presses == presses && trigger.App == app) {
                trigger.Keys.clear();
            }
        }
    }

    /// An index past the end is a new binding, and then the seed says which action it runs.
    void _edit(HWND owner, AppConfig& cfg, const int index, const Binding& seed) {
        const bool isExisting = index < static_cast<int>(cfg.Bindings.size());
        const Binding stored = isExisting ? cfg.Bindings[index] : seed;
        const ActionDesc* const desc = ActionRegistry::Find(stored.ActionId);
        const bool runsCommand = _runsCommand(desc);

        ActionEdit edit{
            .Title = stored.Name.empty() ? "New action" : stored.Name,
            .Name = stored.Name,
            // One stored field behind two rows - which of them the dialog shows is the action's own
            // business, and no action has both
            .Command = runsCommand ? stored.Args : "",
            .Args = runsCommand ? "" : stored.Args,
            // An id no module registered still shows its argument: the binding cannot fire, and a row
            // the user cannot even read is a worse way to say so than a labelled one they can fix
            .ArgsLabel = desc ? (runsCommand ? "" : std::string{desc->ArgsLabel})
                              : (stored.Args.empty() ? "" : "Argument"),
            .ArgsHint = desc ? std::string{desc->ArgsHint} : "",
            .Sound = stored.Sound,
            .SoundVolume = stored.SoundVolume,
            // The box always shows what will actually appear on screen, template included
            .Notification = !stored.Notification.empty() ? stored.Notification
                            : desc ? std::string{desc->DefaultNotification}
                                   : std::string{ActionRegistry::DefaultNotification},
            .App = stored.Trigger.App,
            // The dialog and the capture both work in masks; the name is what goes to disk
            .Hotkey = KeyNames::Parse(stored.Trigger.Keys),
            .OnRelease = stored.Trigger.OnRelease,
            .Presses = stored.Trigger.Presses,
            .Block = stored.Trigger.Block,
            .TapOnly = stored.Trigger.TapOnly,
            .ShowNotification = stored.ShowNotification,
            .RunsCommand = runsCommand,
            .HasSound = !desc || !HasFlag(desc->Flags, ActionFlags::NoSound),
            // Carrying a release factory is what makes "trigger on release" meaningless for an action
            .HoldOnly = desc && desc->MakeRelease != nullptr,
            .AllowDelete = isExisting,
        };

        if (!ActionDialog::Show(GetModuleHandleW(nullptr), owner, edit, cfg.RecentSounds)) {
            return;
        }

        if (edit.Deleted) {
            cfg.Bindings.erase(cfg.Bindings.begin() + index);
            return;
        }

        const std::string keys = edit.Hotkey ? KeyNames::Format(edit.Hotkey) : std::string{};
        const std::string app = Foreground::CanonicalApp(edit.App);
        _clearHotkey(cfg, keys, edit.Presses, app, isExisting ? index : -1);

        // Starts from what was stored so the action id the entry points at survives the edit
        Binding binding = stored;
        binding.Name = edit.Name;
        binding.Args = runsCommand ? edit.Command : edit.Args;
        binding.Sound = edit.Sound;
        binding.Notification = edit.Notification;
        binding.SoundVolume = edit.SoundVolume;
        binding.ShowNotification = edit.ShowNotification;
        binding.Trigger = {.Keys = keys,
                           .Presses = edit.Presses,
                           .OnRelease = edit.OnRelease,
                           .Block = edit.Block,
                           .TapOnly = edit.TapOnly,
                           .App = app};

        if (isExisting) {
            cfg.Bindings[index] = binding;
        } else {
            cfg.Bindings.push_back(binding);
        }
    }

    /**
     * @brief Asks which action the new entry runs, then opens it for editing.
     *
     * An action is an entry like any other, so the list can hold several of the same one - which is
     * the point of picking it here rather than having one fixed row per action.
     */
    void _add(HWND owner, AppConfig& cfg) {
        HMENU menu = CreatePopupMenu();

        const int count = static_cast<int>(ActionRegistry::All.size());
        std::string_view group;
        for (int i = 0; i < count; i++) {
            const ActionDesc& desc = ActionRegistry::All[i];
            // Registration order is the module order in main, so one group's actions are contiguous.
            // Anything that breaks that has to sort the menu rather than separate it.
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

    /// The list notifies its parent, and the page is the grid's - so the page is subclassed for as
    /// long as it lives. A window proc, not a dialog proc: results are returned, not stored.
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
                // Tint the rows that launch a command line, so they stand apart from the rest
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
    const HWND list = SettingsRows::Control(page, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
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
