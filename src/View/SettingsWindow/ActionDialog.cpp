#include "ActionDialog.hpp"

#include "DialogControls.hpp"
#include "Tokens.hpp"
#include "Resources/Resource.h"
#include "../../Audio/AudioFileValidator.hpp"
#include "../../Lib/HotkeyCapture.hpp"
#include "../../Lib/HotkeyManager.hpp"
#include "../../Lib/Str.hpp"

namespace {

    /// Posted to self once the dialog is up - starting the capture from WM_INITDIALOG would arm
    /// the hooks while the dialog manager is still building the window.
    constexpr UINT WM_AUTOBIND = WM_APP + 2;

    struct DialogState {
        ActionEdit* action = nullptr;
        std::set<std::string>* recentSounds = nullptr;
    };

    void SetHotkeyButtonText(HWND dialog, const uint64_t mask) {
        SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY,
                        mask ? Str::Utf8ToWide(HotkeyManager::GetHotkeyName(mask)).c_str()
                             : L"Click to bind");
    }

    /// GetDlgItemTextW needs a fixed buffer, and command lines outgrow any sane guess
    std::wstring GetDlgItemWideString(HWND dialog, int controlId) {
        const HWND control = GetDlgItem(dialog, controlId);
        const int length = GetWindowTextLengthW(control);
        if (length <= 0) {
            return {};
        }
        std::wstring text(length, L'\0');
        const int copied = GetWindowTextW(control, text.data(), length + 1);
        text.resize(copied);
        return text;
    }

    /// While a combination is being typed the dialog manager must not read ESC as Cancel or
    /// ENTER as OK - they are bindable keys like any other.
    LRESULT CALLBACK HotkeyButtonProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR) {
        if (message == WM_GETDLGCODE && HotkeyCapture::IsActive()) {
            return DLGC_WANTALLKEYS;
        }

        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(button, HotkeyButtonProc, id);
        }

        return DefSubclassProc(button, message, wParam, lParam);
    }

    void StartCapture(HWND dialog, ActionEdit& action) {
        SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY, L"Press desired key combination or ESC to clear...");

        const bool started = HotkeyCapture::Start(dialog, [dialog, &action](const uint64_t mask) {
            action.Hotkey = mask;
            SetHotkeyButtonText(dialog, mask);
        });

        if (!started) {
            SetHotkeyButtonText(dialog, action.Hotkey);
        }
    }

    /// Offers the tokens a field accepts with what they mean, and types the chosen one for the user.
    void ShowTokenMenu(HWND dialog, const int editId, const int buttonId, const unsigned field,
                       const bool isCustom) {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }

        for (int i = 0; i < Tokens::Count; i++) {
            const auto& token = Tokens::All[i];
            if (!(token.Fields & field) || (token.CustomOnly && !isCustom)) {
                continue;
            }

            // The tab splits a menu item in two columns - the token, then what it stands for
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
                     reinterpret_cast<LPARAM>(Str::Utf8ToWide(Tokens::All[chosen - 1].Text).c_str()));
    }

    /// The counts a combination can be bound to. More than a handful is not pressable.
    constexpr int MaxPresses = 5;

    int SelectedPresses(HWND dialog) {
        const auto index = SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_GETCURSEL, 0, 0);
        return index == CB_ERR ? 1 : static_cast<int>(index) + 1;
    }

    /// Counting presses happens on the way down, so a counted action cannot also answer the
    /// release - the single press would run on the way up and again when the window closed.
    void SetOnReleaseEnabled(HWND dialog, const bool enabled) {
        if (!enabled) {
            CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, BST_UNCHECKED);
        }
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_ON_RELEASE), enabled);
    }

    /// Only a release can tell a tap from a hold, so the box follows the checkbox that decides it.
    void SyncTapOnly(HWND dialog) {
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_TAP_ONLY),
                     DialogControls::IsChecked(dialog, IDC_ACTION_ON_RELEASE));
    }

    void SetNotificationEnabled(HWND dialog, const bool enabled) {
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_NOTIFICATION), enabled);
        EnableWindow(GetDlgItem(dialog, IDC_ACTION_NOTIFICATION_TOKENS), enabled);
    }

    void LayoutForAction(HWND dialog, const ActionEdit& action) {
        if (!action.IsCustom) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_COMMAND_LABEL, IDC_ACTION_COMMAND,
                                                 IDC_ACTION_COMMAND_TOKENS});
        }

        if (action.ArgsLabel.empty()) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_ARGS_LABEL, IDC_ACTION_ARGS});
        }

        if (!action.HasSound) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_SOUND_LABEL, IDC_ACTION_SOUND,
                                                 IDC_ACTION_SOUND_BROWSE});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_SOUND_VOLUME_LABEL,
                                                 IDC_ACTION_SOUND_VOLUME});
        }

        if (action.HoldOnly) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_PRESSES_LABEL, IDC_ACTION_PRESSES});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_ON_RELEASE});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_TAP_ONLY});
        }

        ShowWindow(GetDlgItem(dialog, IDC_ACTION_DELETE), action.AllowDelete ? SW_SHOW : SW_HIDE);
    }

    INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<DialogState*>(lParam);
                SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
                ActionEdit& action = *state->action;

                SetWindowTextW(dialog, Str::Utf8ToWide(action.Title).c_str());
                SetDlgItemTextW(dialog, IDC_ACTION_NAME, Str::Utf8ToWide(action.Name).c_str());
                SetDlgItemTextW(dialog, IDC_ACTION_COMMAND, Str::Utf8ToWide(action.Command).c_str());
                SetDlgItemTextW(dialog, IDC_ACTION_ARGS, Str::Utf8ToWide(action.Args).c_str());

                if (!action.ArgsLabel.empty()) {
                    SetDlgItemTextW(dialog, IDC_ACTION_ARGS_LABEL,
                                    Str::Utf8ToWide(action.ArgsLabel).c_str());
                    // Every action reads its argument its own way, so the format has to be on
                    // screen rather than in a manual nobody has
                    SendDlgItemMessageW(dialog, IDC_ACTION_ARGS, EM_SETCUEBANNER, TRUE,
                        reinterpret_cast<LPARAM>(Str::Utf8ToWide(action.ArgsHint).c_str()));
                }

                SetDlgItemTextW(dialog, IDC_ACTION_NOTIFICATION, Str::Utf8ToWide(action.Notification).c_str());
                CheckDlgButton(dialog, IDC_ACTION_NOTIFICATION_ENABLED,
                               action.ShowNotification ? BST_CHECKED : BST_UNCHECKED);
                SetNotificationEnabled(dialog, action.ShowNotification);

                for (int presses = 1; presses <= MaxPresses; presses++) {
                    SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(std::to_wstring(presses).c_str()));
                }
                SendDlgItemMessageW(dialog, IDC_ACTION_PRESSES, CB_SETCURSEL,
                                    (action.Presses ? action.Presses : 1) - 1, 0);
                SetOnReleaseEnabled(dialog, action.Presses <= 1);
                CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, action.OnRelease ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(dialog, IDC_ACTION_TAP_ONLY, action.TapOnly ? BST_CHECKED : BST_UNCHECKED);
                SyncTapOnly(dialog);
                CheckDlgButton(dialog, IDC_ACTION_BLOCK, action.Block ? BST_CHECKED : BST_UNCHECKED);
                SetHotkeyButtonText(dialog, action.Hotkey);
                DialogControls::PopulateSoundCombo(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                   *state->recentSounds, action.Sound);
                DialogControls::InitTrackbar(GetDlgItem(dialog, IDC_ACTION_SOUND_VOLUME),
                                             10, MAKELONG(0, 100), action.SoundVolume);

                LayoutForAction(dialog, action);
                SetWindowSubclass(GetDlgItem(dialog, IDC_ACTION_HOTKEY), HotkeyButtonProc, 0, 0);
                DialogControls::CenterOnScreen(dialog);

                // Nothing bound yet - the user came here to bind, so skip the extra click
                if (!action.Hotkey) {
                    PostMessageW(dialog, WM_AUTOBIND, 0, 0);
                }
                return TRUE;
            }

            case WM_AUTOBIND:
                if (state) {
                    StartCapture(dialog, *state->action);
                }
                return TRUE;

            // Formatting the name is the dialog's job - the hook only says the mask changed
            case HotkeyCapture::WM_CAPTURE_PREVIEW:
                SetDlgItemTextW(dialog, IDC_ACTION_HOTKEY,
                                Str::Utf8ToWide(HotkeyManager::GetHotkeyName(HotkeyCapture::CapturedMask())).c_str());
                return TRUE;

            case HotkeyCapture::WM_CAPTURE_DONE:
                HotkeyCapture::Finish();
                return TRUE;

            case WM_DESTROY:
                HotkeyCapture::Cancel();
                break;

            case WM_COMMAND: {
                if (!state) {
                    break;
                }
                ActionEdit& action = *state->action;

                switch (LOWORD(wParam)) {
                    case IDC_ACTION_HOTKEY:
                        StartCapture(dialog, action);
                        return TRUE;

                    // Turning it off keeps the text, so the wording survives until it is wanted again
                    case IDC_ACTION_NOTIFICATION_ENABLED:
                        SetNotificationEnabled(dialog,
                            DialogControls::IsChecked(dialog, IDC_ACTION_NOTIFICATION_ENABLED));
                        return TRUE;

                    case IDC_ACTION_ON_RELEASE:
                        SyncTapOnly(dialog);
                        return TRUE;

                    case IDC_ACTION_PRESSES:
                        if (HIWORD(wParam) == CBN_SELCHANGE) {
                            SetOnReleaseEnabled(dialog, SelectedPresses(dialog) <= 1);
                            SyncTapOnly(dialog);
                        }
                        return TRUE;

                    case IDC_ACTION_NOTIFICATION_TOKENS:
                        ShowTokenMenu(dialog, IDC_ACTION_NOTIFICATION, IDC_ACTION_NOTIFICATION_TOKENS,
                                      Tokens::Notification, action.IsCustom);
                        return TRUE;

                    case IDC_ACTION_COMMAND_TOKENS:
                        ShowTokenMenu(dialog, IDC_ACTION_COMMAND, IDC_ACTION_COMMAND_TOKENS,
                                      Tokens::Command, action.IsCustom);
                        return TRUE;

                    case IDC_ACTION_SOUND_BROWSE: {
                        std::string selected;
                        if (AudioFileValidator::PickValidWavFile(dialog, "Select action sound file", selected)) {
                            DialogControls::AddRecentSound(*state->recentSounds, selected);
                            DialogControls::PopulateSoundCombo(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                               *state->recentSounds, selected);
                        }
                        return TRUE;
                    }

                    case IDC_ACTION_DELETE:
                        action.Deleted = true;
                        EndDialog(dialog, TRUE);
                        return TRUE;

                    case IDOK: {
                        auto name = Str::WideToUtf8(GetDlgItemWideString(dialog, IDC_ACTION_NAME));
                        auto command = Str::WideToUtf8(GetDlgItemWideString(dialog, IDC_ACTION_COMMAND));

                        if (name.empty() || (action.IsCustom && command.empty())) {
                            MessageBoxW(dialog, action.IsCustom
                                            ? L"Name and command are required."
                                            : L"Name is required.",
                                        L"Action", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        action.Name = std::move(name);
                        action.Command = std::move(command);
                        action.Args = Str::WideToUtf8(GetDlgItemWideString(dialog, IDC_ACTION_ARGS));

                        action.Presses = action.HoldOnly ? uint8_t{1}
                                                        : static_cast<uint8_t>(SelectedPresses(dialog));
                        action.OnRelease = !action.HoldOnly && action.Presses == 1
                                           && IsDlgButtonChecked(dialog, IDC_ACTION_ON_RELEASE) == BST_CHECKED;
                        action.Block = DialogControls::IsChecked(dialog, IDC_ACTION_BLOCK);
                        action.TapOnly = action.OnRelease
                                         && DialogControls::IsChecked(dialog, IDC_ACTION_TAP_ONLY);
                        action.Sound = action.HasSound
                                       ? DialogControls::ResolveSound(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                                      *state->recentSounds)
                                       : std::string{};
                        if (action.HasSound) {
                            action.SoundVolume = static_cast<uint8_t>(SendDlgItemMessageW(
                                dialog, IDC_ACTION_SOUND_VOLUME, TBM_GETPOS, 0, 0));
                        }
                        action.ShowNotification = DialogControls::IsChecked(dialog, IDC_ACTION_NOTIFICATION_ENABLED);
                        action.Notification = Str::WideToUtf8(GetDlgItemWideString(dialog, IDC_ACTION_NOTIFICATION));

                        EndDialog(dialog, TRUE);
                        return TRUE;
                    }

                    case IDCANCEL:
                        EndDialog(dialog, FALSE);
                        return TRUE;
                }
                break;
            }
        }

        return FALSE;
    }
}

bool ActionDialog::Show(HINSTANCE hInstance, HWND owner, ActionEdit& action,
                        std::set<std::string>& recentSounds) {
    DialogState state{.action = &action, .recentSounds = &recentSounds};

    return DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ACTION_EDIT), owner,
                           DialogProc, reinterpret_cast<LPARAM>(&state)) == TRUE;
}
