#include "ActionDialog.hpp"

#include "DialogControls.hpp"
#include "Resources/Resource.h"
#include "../../Audio/AudioFileValidator.hpp"
#include "../../Lib/HotkeyCapture.hpp"
#include "../../Lib/HotkeyManager.hpp"

namespace {

    struct DialogState {
        CustomAction* action = nullptr;
        std::set<std::string>* recentSounds = nullptr;
        bool allowDelete = false;
        bool deleted = false;
        uint64_t mask = 0;
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

    void StartCapture(HWND dialog, DialogState& state) {
        SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, "Press desired key combination or ESC to clear...");

        const bool started = HotkeyCapture::Start(dialog,
            [dialog](const std::string& hotkeyName) {
                SetDlgItemTextA(dialog, IDC_ACTION_HOTKEY, hotkeyName.c_str());
            },
            [dialog, &state](const uint64_t mask) {
                state.mask = mask;
                SetHotkeyButtonText(dialog, mask);
            });

        if (!started) {
            SetHotkeyButtonText(dialog, state.mask);
        }
    }

    INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

        switch (message) {
            case WM_INITDIALOG: {
                state = reinterpret_cast<DialogState*>(lParam);
                SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);

                SetDlgItemTextA(dialog, IDC_ACTION_NAME, state->action->Name.c_str());
                SetDlgItemTextA(dialog, IDC_ACTION_COMMAND, state->action->Command.c_str());
                CheckDlgButton(dialog, IDC_ACTION_ON_RELEASE, state->action->OnRelease ? BST_CHECKED : BST_UNCHECKED);
                SetHotkeyButtonText(dialog, state->mask);
                DialogControls::PopulateSourceComboBox(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                       *state->recentSounds, state->action->Sound, "None");
                ShowWindow(GetDlgItem(dialog, IDC_ACTION_DELETE), state->allowDelete ? SW_SHOW : SW_HIDE);
                SetWindowSubclass(GetDlgItem(dialog, IDC_ACTION_HOTKEY), HotkeyButtonProc, 0, 0);
                DialogControls::CenterOnScreen(dialog);
                return TRUE;
            }
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

                switch (LOWORD(wParam)) {
                    case IDC_ACTION_HOTKEY:
                        StartCapture(dialog, *state);
                        return TRUE;

                    case IDC_ACTION_SOUND_BROWSE: {
                        std::string selected;
                        if (AudioFileValidator::PickValidWavFile(dialog, "Select action sound file", selected)) {
                            DialogControls::AddToRecentSources(*state->recentSounds, selected);
                            DialogControls::PopulateSourceComboBox(GetDlgItem(dialog, IDC_ACTION_SOUND),
                                                                   *state->recentSounds, selected, "None");
                        }
                        return TRUE;
                    }

                    case IDC_ACTION_DELETE:
                        state->deleted = true;
                        EndDialog(dialog, TRUE);
                        return TRUE;

                    case IDOK: {
                        auto name = GetDlgItemString(dialog, IDC_ACTION_NAME);
                        auto command = GetDlgItemString(dialog, IDC_ACTION_COMMAND);

                        if (name.empty() || command.empty()) {
                            MessageBoxW(dialog, L"Name and command are required.", L"Action", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }

                        state->action->Name = std::move(name);
                        state->action->Command = std::move(command);
                        state->action->OnRelease = IsDlgButtonChecked(dialog, IDC_ACTION_ON_RELEASE) == BST_CHECKED;
                        state->action->Hotkey = state->mask;
                        state->action->Sound = DialogControls::ResolveSourceFromComboBox(
                            GetDlgItem(dialog, IDC_ACTION_SOUND), *state->recentSounds);

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

bool ActionDialog::Show(HINSTANCE hInstance, HWND owner, CustomAction& action,
                        std::set<std::string>& recentSounds, const bool allowDelete, bool& deleted) {
    DialogState state{.action = &action, .recentSounds = &recentSounds,
                      .allowDelete = allowDelete, .mask = action.Hotkey};

    const INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ACTION_EDIT), owner,
                                           DialogProc, reinterpret_cast<LPARAM>(&state));
    deleted = state.deleted;
    return result == TRUE;
}
