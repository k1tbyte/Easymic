#include "ActionDialog.hpp"

#include "DialogControls.hpp"
#include "Resources/Resource.h"
#include "../../Audio/AudioFileValidator.hpp"
#include "../../Lib/HotkeyCapture.hpp"
#include "../../Lib/HotkeyManager.hpp"
#include "../../Lib/Str.hpp"

namespace {

    /// Posted to self once the dialog is up - starting the capture from WM_INITDIALOG would arm
    /// the hooks while the dialog manager is still building the window.
    constexpr UINT WM_AUTOBIND = WM_APP + 1;

    struct DialogState {
        ActionEdit* action = nullptr;
        std::set<std::string>* recentSounds = nullptr;
    };

    void SetHotkeyButtonText(HWND dialog, const uint64_t mask) {
        SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY,
                        mask ? HotkeyManager::GetHotkeyName(mask).c_str() : "Click to bind");
    }

    /// GetDlgItemTextA needs a fixed buffer, and command lines outgrow any sane guess
    std::string GetDlgItemString(HWND dialog, int controlId) {
        const HWND control = GetDlgItem(dialog, controlId);
        std::string text(GetWindowTextLengthA(control), '\0');
        const int copied = GetWindowTextA(control, text.data(), static_cast<int>(text.size()) + 1);
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
        SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, "Press desired key combination or ESC to clear...");

        const bool started = HotkeyCapture::Start(dialog,
            [dialog](const std::string& hotkeyName) {
                SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, hotkeyName.c_str());
            },
            [dialog, &action](const uint64_t mask) {
                action.Hotkey = mask;
                SetHotkeyButtonText(dialog, mask);
            });

        if (!started) {
            SetHotkeyButtonText(dialog, action.Hotkey);
        }
    }

    void LayoutForAction(HWND dialog, const ActionEdit& action) {
        if (!action.IsCustom) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_COMMAND_LABEL, IDC_ACTION_COMMAND});
            DialogControls::CollapseRow(dialog, {IDC_ACTION_NAME_LABEL, IDC_ACTION_NAME});
        }

        if (!action.HasSound) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_SOUND_LABEL, IDC_ACTION_SOUND,
                                                 IDC_ACTION_SOUND_BROWSE});
        }

        if (action.HoldOnly) {
            DialogControls::CollapseRow(dialog, {IDC_ACTION_ON_RELEASE});
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

                SetWindowTextW(dialog, Str::ToWide(action.Title).c_str());
                SetDlgItemTextA(dialog, IDC_ACTION_NAME, action.Name.c_str());
                SetDlgItemTextA(dialog, IDC_ACTION_COMMAND, action.Command.c_str());
                CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, action.OnRelease ? BST_CHECKED : BST_UNCHECKED);
                SetHotkeyButtonText(dialog, action.Hotkey);
                DialogControls::PopulateSoundCombo(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                   *state->recentSounds, action.Sound);

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
                        if (action.IsCustom) {
                            auto name = GetDlgItemString(dialog, IDC_ACTION_NAME);
                            auto command = GetDlgItemString(dialog, IDC_ACTION_COMMAND);

                            if (name.empty() || command.empty()) {
                                MessageBoxW(dialog, L"Name and command are required.", L"Action",
                                            MB_OK | MB_ICONWARNING);
                                return TRUE;
                            }

                            action.Name = std::move(name);
                            action.Command = std::move(command);
                        }

                        action.OnRelease = !action.HoldOnly
                                           && IsDlgButtonChecked(dialog, IDC_ACTION_ON_RELEASE) == BST_CHECKED;
                        action.Sound = action.HasSound
                                       ? DialogControls::ResolveSound(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                                      *state->recentSounds)
                                       : std::string{};

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
