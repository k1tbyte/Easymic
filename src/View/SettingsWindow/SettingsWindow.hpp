#ifndef EASYMIC_SETTINGSWINDOW_V2_HPP
#define EASYMIC_SETTINGSWINDOW_V2_HPP

#include "../Core/BaseWindow.hpp"
#include "Resources/Resource.h"

/**
 * @brief Settings window - empty template
 */
class SettingsWindow : public BaseWindow {

    constexpr static int _tabs[] = {
        IDD_SETTINGS_TAB_GENERAL,
        IDD_SETTINGS_TAB_HOTKEYS
    };

public:
    struct Config {
        HWND parentHwnd = nullptr;
        int dialogResourceId = 0;
    };

    explicit SettingsWindow(HINSTANCE hInstance);
    ~SettingsWindow() override = default;

    bool Initialize(const Config& config);

    void Show() override;
    void Hide() override;

private:
    void SetupMessageHandlers();

    LRESULT OnInitDialog(WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnDestroy(WPARAM wParam, LPARAM lParam);

    Config config_;
};

#endif //EASYMIC_SETTINGSWINDOW_V2_HPP

