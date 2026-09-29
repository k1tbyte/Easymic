#include "ActionDialog.hpp"

#include "AudioFileValidator.hpp"
#include "Windowing/Controls.hpp"
#include "Core/Hotkeys/HotkeyCapture.hpp"
#include "Core/Hotkeys/KeyNames.hpp"
#include "DialogControls.hpp"
#include "Platform/Windowing/WindowCatalog.hpp"
#include "Resources/Resource.h"
#include "Str.hpp"
#include "Tokens.hpp"

namespace {

    /// Posted once the dialog is up: capturing from WM_INITDIALOG would arm the hooks mid-build.
    constexpr UINT WM_AUTOBIND = WM_APP + 2;
    constexpr int MaxPresses = 5;

    struct DialogState {
        Binding& binding;
        const ActionLayout& layout;
        std::set<std::string>& recentSounds;
    };

    void _setText(HWND dialog, const int id, const std::string& text) {
        SetDlgItemTextW(dialog, id, Str::Utf8ToWide(text).c_str());
    }

    std::string _text(HWND dialog, const int id) {
        return Str::WideToUtf8(Controls::Text(GetDlgItem(dialog, id)));
    }

    void _setChecked(HWND dialog, const int id, const bool checked) {
        CheckDlgButton(dialog, id, checked ? BST_CHECKED : BST_UNCHECKED);
    }

    void _setHotkeyText(HWND dialog, const std::string& keys) {
        SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY, keys.empty() ? L"Click to bind" : Str::Utf8ToWide(keys).c_str());
    }

    void _populateAppCombo(HWND dialog) {
        const HWND combo = GetDlgItem(dialog, IDC_ACTION_APP);
        const std::wstring text = Controls::Text(combo);

        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& name : WindowCatalog::AppNames()) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        SetWindowTextW(combo, text.c_str());
    }

    /// While a combination is typed the dialog manager must not read ESC as Cancel or ENTER as OK.
    LRESULT CALLBACK _hotkeyButtonProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR) {
        if (message == WM_GETDLGCODE && HotkeyCapture::IsActive()) {
            return DLGC_WANTALLKEYS;
        }

        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(button, _hotkeyButtonProc, id);
        }

        return DefSubclassProc(button, message, wParam, lParam);
    }

    void _startCapture(HWND dialog, Binding& binding) {
        SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY, L"Press desired key combination or ESC to clear...");

        const bool started = HotkeyCapture::Start(dialog, [dialog, &binding](const uint64_t mask) {
            binding.Trigger.Keys = mask ? KeyNames::Format(mask) : std::string{};
            _setHotkeyText(dialog, binding.Trigger.Keys);
        });

        if (!started) {
            _setHotkeyText(dialog, binding.Trigger.Keys);
        }
    }

    /// Types the chosen token into the field for the user.
    void _showTokenMenu(HWND dialog, const int editId, const int buttonId, const unsigned field,
                        const bool isCustom) {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }

        const auto tokens = Tokens::All();
        for (size_t i = 0; i < tokens.size(); i++) {
            const auto& token = tokens[i];
            if (!(token.Fields & field) || (token.CustomOnly && !isCustom)) {
                continue;
            }

            // The tab splits the item in two columns: the token, then what it stands for
            AppendMenuW(menu, MF_STRING, i + 1,
                        Str::Utf8ToWide(std::string{token.Text} + "\t" + token.Description).c_str());
        }

        RECT button;
        GetWindowRect(GetDlgItem(dialog, buttonId), &button);

        const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_NONOTIFY,
                                          button.right, button.bottom, 0, dialog, nullptr);
        DestroyMenu(menu);

        if (!chosen) {
            return;
        }

        const HWND edit = GetDlgItem(dialog, editId);
        SetFocus(edit);
        SendMessageW(edit, EM_REPLACESEL, TRUE,
                     reinterpret_cast<LPARAM>(Str::Utf8ToWide(tokens[chosen - 1].Text).c_str()));
    }

    void _browseSound(HWND dialog, std::set<std::string>& recentSounds) {
        std::string selected;
        if (AudioFileValidator::PickValidWavFile(dialog, "Select action sound file", selected)) {
            DialogControls::AddRecentSound(recentSounds, selected);
            DialogControls::PopulateSoundCombo(GetDlgItem(dialog, IDC_ACTION_SOUND), recentSounds, selected);
        }
    }

    int _selectedPresses(HWND dialog) {
        const auto index = SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_GETCURSEL, 0, 0);
        return index == CB_ERR ? 1 : static_cast<int>(index) + 1;
    }

    /// Presses are counted on the way down, so a counted action cannot also answer the release.
    void _setOnReleaseEnabled(HWND dialog, const bool enabled) {
        if (!enabled) {
            CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, BST_UNCHECKED);
        }
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_ON_RELEASE), enabled);
    }

    void _syncTapOnly(HWND dialog) {
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_TAP_ONLY),
                     DialogControls::IsChecked(dialog, IDC_ACTION_ON_RELEASE));
    }

    void _setNotificationEnabled(HWND dialog, const bool enabled) {
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_NOTIFICATION), enabled);
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_NOTIFICATION_TOKENS), enabled);
    }

    void _collapseUnusedRows(HWND dialog, const ActionLayout& layout) {
        if (!layout.RunsCommand) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_COMMAND_LABEL, IDC_ACTION_COMMAND,
                                                 IDC_ACTION_COMMAND_TOKENS});
        }

        if (layout.ArgsLabel.empty()) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_ARGS_LABEL, IDC_ACTION_ARGS});
        }

        if (!layout.HasSound) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_SOUND_LABEL, IDC_ACTION_SOUND,
                                                 IDC_ACTION_SOUND_BROWSE});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_SOUND_VOLUME_LABEL, IDC_ACTION_SOUND_VOLUME});
        }

        if (layout.HoldOnly) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_PRESSES_LABEL, IDC_ACTION_PRESSES});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_ON_RELEASE});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_TAP_ONLY});
        }

        ShowWindow(GetDlgItem(dialog, IDC_ACTION_DELETE), layout.AllowDelete ? SW_SHOW : SW_HIDE);
    }

    void _fill(HWND dialog, const DialogState& state) {
        const Binding& binding = state.binding;
        const ActionLayout& layout = state.layout;
        const HotkeyTrigger& trigger = binding.Trigger;

        SetWindowTextW(dialog, Str::Utf8ToWide(layout.Title).c_str());
        _setText(dialog, IDC_ACTION_NAME, binding.Name);
        _setText(dialog, layout.RunsCommand ? IDC_ACTION_COMMAND : IDC_ACTION_ARGS, binding.Args);

        if (!layout.ArgsLabel.empty()) {
            _setText(dialog, IDC_ACTION_ARGS_LABEL, layout.ArgsLabel);
            // Every action reads its argument its own way: the format belongs on screen, not in a manual
            SendDlgItemMessageW(dialog, IDC_ACTION_ARGS, EM_SETCUEBANNER, TRUE,
                                reinterpret_cast<LPARAM>(Str::Utf8ToWide(layout.ArgsHint).c_str()));
        }

        _setText(dialog, IDC_ACTION_NOTIFICATION, binding.Notification);
        _setChecked(dialog, IDC_ACTION_NOTIFICATION_ENABLED, binding.ShowNotification);
        _setNotificationEnabled(dialog, binding.ShowNotification);

        for (int presses = 1; presses <= MaxPresses; presses++) {
            SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(std::to_wstring(presses).c_str()));
        }
        SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_SETCURSEL, (trigger.Presses ? trigger.Presses : 1) - 1, 0);
        _setOnReleaseEnabled(dialog, trigger.Presses <= 1);
        _setChecked(dialog, IDC_ACTION_ON_RELEASE, trigger.OnRelease);
        _setChecked(dialog, IDC_ACTION_TAP_ONLY, trigger.TapOnly);
        _syncTapOnly(dialog);
        _setChecked(dialog, IDC_ACTION_BLOCK, trigger.Block);
        _setText(dialog, IDC_ACTION_APP, trigger.App);
        SendDlgItemMessageW(dialog, IDC_ACTION_APP, CB_SETCUEBANNER, 0,
                            reinterpret_cast<LPARAM>(L"e.g. chrome.exe (empty = global)"));

        _setHotkeyText(dialog, trigger.Keys);
        DialogControls::PopulateSoundCombo(GetDlgItem(dialog, IDC_ACTION_SOUND), state.recentSounds, binding.Sound);
        DialogControls::InitTrackbar(GetDlgItem(dialog, IDC_ACTION_SOUND_VOLUME), 10, MAKELONG(0, 100),
                                     binding.SoundVolume);

        _collapseUnusedRows(dialog, layout);
        SetWindowSubclass(GetDlgItem(dialog, IDC_ACTION_HOTKEY), _hotkeyButtonProc, 0, 0);
        DialogControls::CenterOnScreen(dialog);

        // Nothing bound yet: the user came to bind, so skip the extra click
        if (trigger.Keys.empty()) {
            PostMessageW(dialog, WM_AUTOBIND, 0, 0);
        }
    }

    /// False when the dialog stays open because a required field is empty.
    bool _read(HWND dialog, const DialogState& state) {
        Binding& binding = state.binding;
        const ActionLayout& layout = state.layout;
        HotkeyTrigger& trigger = binding.Trigger;

        std::string name = _text(dialog, IDC_ACTION_NAME);
        std::string args = _text(dialog, layout.RunsCommand ? IDC_ACTION_COMMAND : IDC_ACTION_ARGS);
        if (name.empty() || (layout.RunsCommand && args.empty())) {
            MessageBoxW(dialog, layout.RunsCommand ? L"Name and command are required." : L"Name is required.",
                        L"Action", MB_OK | MB_ICONWARNING);
            return false;
        }

        binding.Name = std::move(name);
        binding.Args = std::move(args);
        trigger.App = _text(dialog, IDC_ACTION_APP);
        trigger.Presses = layout.HoldOnly ? uint8_t{1} : static_cast<uint8_t>(_selectedPresses(dialog));
        trigger.OnRelease = !layout.HoldOnly && trigger.Presses == 1
                            && DialogControls::IsChecked(dialog, IDC_ACTION_ON_RELEASE);
        trigger.Block = DialogControls::IsChecked(dialog, IDC_ACTION_BLOCK);
        trigger.TapOnly = trigger.OnRelease && DialogControls::IsChecked(dialog, IDC_ACTION_TAP_ONLY);

        binding.Sound = layout.HasSound
                        ? DialogControls::ResolveSound(GetDlgItem(dialog, IDC_ACTION_SOUND), state.recentSounds)
                        : std::string{};
        if (layout.HasSound) {
            binding.SoundVolume = static_cast<uint8_t>(SendDlgItemMessageW(dialog, IDC_ACTION_SOUND_VOLUME,
                                                                           TBM_GETPOS, 0, 0));
        }
        binding.ShowNotification = DialogControls::IsChecked(dialog, IDC_ACTION_NOTIFICATION_ENABLED);
        binding.Notification = _text(dialog, IDC_ACTION_NOTIFICATION);
        return true;
    }

    bool _onCommand(HWND dialog, const DialogState& state, const WPARAM wParam) {
        switch (LOWORD(wParam)) {
            case IDC_ACTION_HOTKEY:
                _startCapture(dialog, state.binding);
                return true;

            // Turning it off keeps the text, so the wording survives until it is wanted again
            case IDC_ACTION_NOTIFICATION_ENABLED:
                _setNotificationEnabled(dialog, DialogControls::IsChecked(dialog, IDC_ACTION_NOTIFICATION_ENABLED));
                return true;

            case IDC_ACTION_ON_RELEASE:
                _syncTapOnly(dialog);
                return true;

            case IDC_ACTION_PRESSES:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    _setOnReleaseEnabled(dialog, _selectedPresses(dialog) <= 1);
                    _syncTapOnly(dialog);
                }
                return true;

            case IDC_ACTION_APP:
                if (HIWORD(wParam) == CBN_DROPDOWN) {
                    if (HotkeyCapture::IsActive()) {
                        HotkeyCapture::Cancel();
                        _setHotkeyText(dialog, state.binding.Trigger.Keys);
                    }
                    _populateAppCombo(dialog);
                }
                return true;

            case IDC_ACTION_NOTIFICATION_TOKENS:
                _showTokenMenu(dialog, IDC_ACTION_NOTIFICATION, IDC_ACTION_NOTIFICATION_TOKENS,
                               Tokens::Notification, state.layout.RunsCommand);
                return true;

            case IDC_ACTION_COMMAND_TOKENS:
                _showTokenMenu(dialog, IDC_ACTION_COMMAND, IDC_ACTION_COMMAND_TOKENS,
                               Tokens::Command, state.layout.RunsCommand);
                return true;

            case IDC_ACTION_SOUND_BROWSE:
                _browseSound(dialog, state.recentSounds);
                return true;

            case IDC_ACTION_DELETE:
            case IDCANCEL:
                EndDialog(dialog, LOWORD(wParam));
                return true;

            case IDOK:
                if (_read(dialog, state)) {
                    EndDialog(dialog, IDOK);
                }
                return true;
        }
        return false;
    }

    INT_PTR CALLBACK _dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG:
                SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
                _fill(dialog, *reinterpret_cast<DialogState*>(lParam));
                return TRUE;

            case WM_AUTOBIND:
                if (state) {
                    _startCapture(dialog, state->binding);
                }
                return TRUE;

            // The hook only says the mask changed: formatting it is the dialog's job
            case HotkeyCapture::WM_CAPTURE_PREVIEW:
                SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY,
                                Str::Utf8ToWide(KeyNames::Format(HotkeyCapture::CapturedMask())).c_str());
                return TRUE;

            case HotkeyCapture::WM_CAPTURE_DONE:
                HotkeyCapture::Finish();
                return TRUE;

            case WM_DESTROY:
                HotkeyCapture::Cancel();
                return FALSE;

            case WM_COMMAND:
                return state && _onCommand(dialog, *state, wParam);

            default:
                return FALSE;
        }
    }
}

ActionDialog::Result ActionDialog::Show(HINSTANCE hInstance, HWND owner, Binding& binding,
                                        const ActionLayout& layout, std::set<std::string>& recentSounds) {
    DialogState state{binding, layout, recentSounds};

    switch (DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ACTION_EDIT), owner, _dialogProc,
                            reinterpret_cast<LPARAM>(&state))) {
        case IDOK:              return Result::Save;
        case IDC_ACTION_DELETE: return Result::Delete;
        default:                return Result::Cancel;
    }
}
