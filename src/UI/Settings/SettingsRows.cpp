#include "SettingsRows.hpp"

#include <commctrl.h>

#include "AudioFileValidator.hpp"
#include "Controls.hpp"
#include "DialogControls.hpp"

namespace {
    // Dialog units - the grid the DIALOGEX pages were drawn on
    constexpr int LabelWidth = 95;
    constexpr int ControlLeft = 100;
    constexpr int Gap = 4;
    constexpr int TextHeight = 8;
    constexpr int GroupTitle = 13;
    constexpr int GroupBottom = 6;
    constexpr int GroupIndent = 7;
    constexpr int BrowseWidth = 16;
    constexpr int DropHeight = 70;
    constexpr int RadioPitch = 12;

    /// A block of ids per row: a SoundPicker's browse button is its combo's id plus one, a Radio's
    /// choice k is its first button's id plus k.
    constexpr int FirstId = 1000;
    constexpr int IdsPerRow = 16;
    int RowId(size_t index) { return FirstId + static_cast<int>(index) * IdsPerRow; }

    int HeightOf(const SettingsRow& row) {
        switch (row.Kind) {
            case RowKind::Slider:      return 15;
            case RowKind::Combo:
            case RowKind::SoundPicker: return 13;
            case RowKind::Radio:       return static_cast<int>(row.Items().size() + 1) * RadioPitch - 2;
            case RowKind::Custom:      return row.Height;
            default:                   return 10;
        }
    }

    /// Build's final y: a group adds its title and bottom, any other row its height and a gap.
    int ContentHeight(std::span<const SettingsRow> rows) {
        int height = 0;
        for (const SettingsRow& row : rows) {
            height += row.Kind == RowKind::Group ? GroupTitle + GroupBottom : HeightOf(row) + Gap;
        }
        return height;
    }

    /// Runs before the rows take their width from the page, since the bar narrows it.
    void FitScrollBar(HWND page, std::span<const SettingsRow> rows) {
        RECT content{0, 0, 0, ContentHeight(rows)};
        MapDialogRect(page, &content);
        RECT client;
        GetClientRect(page, &client);
        if (content.bottom <= client.bottom) {
            return;
        }

        const SCROLLINFO info{.cbSize = sizeof(info), .fMask = SIF_RANGE | SIF_PAGE,
                              .nMax = content.bottom - 1, .nPage = static_cast<UINT>(client.bottom)};
        SetScrollInfo(page, SB_VERT, &info, FALSE);
        ShowScrollBar(page, SB_VERT, TRUE);
    }

    /// The page's client width in dialog units, so a row fits the page rather than the template
    /// the page used to be.
    int PageWidth(HWND page) {
        RECT client;
        GetClientRect(page, &client);
        RECT unit{0, 0, 100, 0};
        MapDialogRect(page, &unit);
        return MulDiv(client.right, 100, unit.right);
    }

    int ReadInt(const SettingsRow& row, HWND control) {
        switch (row.Kind) {
            case RowKind::Check: return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            case RowKind::Combo: return static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
            default:             return static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
        }
    }

    /// A picker maps a selection back to RecentSounds by position, so a list that no longer has
    /// one item per entry resolves to the wrong file even while its selection still reads right.
    bool SoundListStale(HWND combo, const SettingsRow& row, AppConfig& cfg) {
        const auto listed = static_cast<size_t>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        return listed != 1 + std::size(SoundCatalog::All) + cfg.RecentSounds.size()
               || DialogControls::ResolveSound(combo, cfg.RecentSounds) != row.Field.Text(cfg);
    }

    /// Writes the field into its control only when the control shows something else - a slider
    /// mid-drag is never written back, and a control fresh from Build differs unless blank is
    /// already right, so the same pass fills a new page in.
    void Sync(HWND page, const SettingsRow& row, int id, AppConfig& cfg) {
        HWND control = GetDlgItem(page, id);
        if (!control) {
            return;
        }

        if (row.Enabled) {
            const BOOL enabled = row.Enabled(cfg);
            for (int part = 0; part < IdsPerRow; part++) {
                if (HWND piece = GetDlgItem(page, id + part)) {
                    EnableWindow(piece, enabled);
                }
            }
        }

        if (row.Kind == RowKind::Radio) {
            const int value = row.Field.Get(cfg);
            for (int part = 0; part < IdsPerRow; part++) {
                HWND button = GetDlgItem(page, id + part);
                if (!button) {
                    break;
                }
                const LRESULT state = part == value ? BST_CHECKED : BST_UNCHECKED;
                if (SendMessageW(button, BM_GETCHECK, 0, 0) != state) {
                    SendMessageW(button, BM_SETCHECK, state, 0);
                }
            }
            return;
        }

        if (row.Field.Text) {
            if (SoundListStale(control, row, cfg)) {
                DialogControls::PopulateSoundCombo(control, cfg.RecentSounds, row.Field.Text(cfg));
            }
            return;
        }

        if (!row.Field.Get) {
            return;
        }

        const int value = row.Field.Get(cfg);
        if (ReadInt(row, control) == value) {
            return;
        }

        switch (row.Kind) {
            case RowKind::Check:  SendMessageW(control, BM_SETCHECK, value, 0); break;
            case RowKind::Combo:  SendMessageW(control, CB_SETCURSEL, value, 0); break;
            case RowKind::Slider: SendMessageW(control, TBM_SETPOS, TRUE, value); break;
            default:              break;
        }
    }

    void SyncAll(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg) {
        for (size_t i = 0; i < rows.size(); i++) {
            Sync(page, rows[i], RowId(i), cfg);
        }
    }
}

HWND SettingsRows::Control(HWND page, const wchar_t* cls, const wchar_t* text, DWORD style,
                           RECT cell, int id, DWORD exStyle) {
    return Controls::Create(page, page, cls, text, style, cell, id, exStyle);
}

void SettingsRows::Build(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg) {
    FitScrollBar(page, rows);
    const int width = PageWidth(page);
    int y = 0;
    HWND group = nullptr;
    int groupTop = 0;

    // A group box cannot know its height until the row after its last one
    const auto closeGroup = [&] {
        if (!group) {
            return;
        }
        const int bottom = y - Gap + GroupBottom;
        RECT box{0, groupTop, width, bottom};
        MapDialogRect(page, &box);
        SetWindowPos(group, nullptr, 0, 0, box.right - box.left, box.bottom - box.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        y = bottom + Gap;
        group = nullptr;
    };

    for (size_t i = 0; i < rows.size(); i++) {
        const SettingsRow& row = rows[i];
        const int id = RowId(i);

        if (row.Kind == RowKind::Group) {
            closeGroup();
            groupTop = y;
            group = Control(page, WC_BUTTONW, row.Label, BS_GROUPBOX, {0, y, width, y + GroupTitle}, id);
            y += GroupTitle;
            continue;
        }

        const int left = group ? GroupIndent : 0;
        const int right = width - left;
        const int height = HeightOf(row);
        const int labelTop = row.Kind == RowKind::Radio ? y + 1 : y + (height - TextHeight) / 2;

        if (row.Kind != RowKind::Check && row.Kind != RowKind::Text && row.Kind != RowKind::Custom) {
            Control(page, WC_STATICW, row.Label, SS_LEFT, {left, labelTop, LabelWidth, labelTop + TextHeight}, -1);
        }

        switch (row.Kind) {
            case RowKind::Check:
                Control(page, WC_BUTTONW, row.Label, BS_AUTOCHECKBOX | WS_TABSTOP, {left, y, right, y + height}, id);
                break;

            case RowKind::Text:
                Control(page, WC_STATICW, row.Label, SS_LEFT, {left, y, right, y + height}, id);
                break;

            case RowKind::Combo: {
                HWND combo = Control(page, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                     {ControlLeft, y, right, y + DropHeight}, id);
                for (const wchar_t* item : row.Items()) {
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
                }
                break;
            }

            case RowKind::Slider: {
                HWND slider = Control(page, TRACKBAR_CLASSW, L"",
                                      TBS_AUTOTICKS | TBS_ENABLESELRANGE | TBS_TOOLTIPS | WS_TABSTOP,
                                      {ControlLeft, y, right, y + height}, id);
                SendMessageW(slider, TBM_SETRANGE, FALSE, MAKELPARAM(row.Min, row.Max));
                SendMessageW(slider, TBM_SETPAGESIZE, 0, row.Step);
                break;
            }

            case RowKind::SoundPicker:
                Control(page, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                        {ControlLeft, y, right - BrowseWidth - 4, y + DropHeight}, id);
                Control(page, WC_BUTTONW, L"...", BS_PUSHBUTTON | WS_TABSTOP,
                        {right - BrowseWidth, y, right, y + height}, id + 1);
                break;

            case RowKind::Radio: {
                const auto items = row.Items();
                for (int part = 0; part < static_cast<int>(items.size()) && part < IdsPerRow; part++) {
                    const int top = y + (part + 1) * RadioPitch;
                    Control(page, WC_BUTTONW, items[part], BS_AUTORADIOBUTTON | (part ? 0 : WS_GROUP | WS_TABSTOP),
                            {left, top, right, top + TextHeight + 2}, id + part);
                }
                break;
            }

            case RowKind::Custom:
                row.Create(page, {left, y, right, y + height}, id);
                break;

            default:
                break;
        }

        y += height + Gap;
    }

    closeGroup();
    SyncAll(page, rows, cfg);
}

void SettingsRows::Handle(HWND page, std::span<const SettingsRow> rows, AppConfig& cfg,
                          UINT message, WPARAM wParam, LPARAM lParam) {
    HWND control = reinterpret_cast<HWND>(lParam);
    if (!control) {
        return;
    }

    const int offset = GetDlgCtrlID(control) - FirstId;
    if (offset < 0 || static_cast<size_t>(offset / IdsPerRow) >= rows.size()) {
        return;
    }

    const SettingsRow& row = rows[offset / IdsPerRow];
    const int part = offset % IdsPerRow;
    const bool scroll = message == WM_HSCROLL;
    const WORD code = scroll ? LOWORD(wParam) : HIWORD(wParam);
    const HWND owner = GetAncestor(page, GA_ROOT);

    switch (row.Kind) {
        case RowKind::Check:
        case RowKind::Combo:
            if (scroll || code != (row.Kind == RowKind::Check ? BN_CLICKED : CBN_SELCHANGE)) {
                return;
            }
            row.Field.Set(cfg, ReadInt(row, control));
            break;

        case RowKind::Slider:
            if (!scroll || code == TB_ENDTRACK) {
                return;
            }
            row.Field.Set(cfg, ReadInt(row, control));
            break;

        case RowKind::SoundPicker:
            if (part) {
                std::string file;
                if (code != BN_CLICKED || !AudioFileValidator::PickValidWavFile(owner, "Select sound file", file)) {
                    return;
                }
                DialogControls::AddRecentSound(cfg.RecentSounds, file);
                row.Field.Text(cfg) = file;
            } else {
                if (code != CBN_SELCHANGE) {
                    return;
                }
                row.Field.Text(cfg) = DialogControls::ResolveSound(control, cfg.RecentSounds);
            }
            break;

        case RowKind::Radio:
            if (scroll || code != BN_CLICKED) {
                return;
            }
            row.Field.Set(cfg, part);
            break;

        case RowKind::Custom:
            if (scroll || code != BN_CLICKED) {
                return;
            }
            break;

        default:
            return;
    }

    if (row.Changed) {
        row.Changed(owner, cfg);
    }
    SyncAll(page, rows, cfg);
}
