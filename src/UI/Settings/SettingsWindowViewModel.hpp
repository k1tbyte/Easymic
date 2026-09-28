#pragma once

#include <functional>
#include <span>
#include <vector>

#include "Core/ActionRegistry.hpp"
#include "Core/SettingsHost.hpp"
#include "ViewModel.hpp"
#include "Settings/SettingsWindow.hpp"
#include "BaseWindow.hpp"
#include "AppConfig.hpp"
#include "MainWindow.hpp"
#include "Logger.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    AppConfig& _cfg;
    std::function<void()> _captureOverlayPosition;
    std::function<void()> _previewFont;
    std::vector<std::wstring> _fontNames;
    std::vector<const wchar_t*> _fontItems;
    void LoadFonts();
    /// What OK compares against and Cancel puts back - the whole config, so no row needs an undo.
    AppConfig _cfgPrev;
    MainWindow* _mainWindow = nullptr;
    /// The open page's rows, which its controls' input is routed through.
    std::span<const SettingsRow> _rows;

    // Logger UI management
    int _logAddedSubscriptionId = -1;
    HFONT _linkFont = nullptr;

    void RefreshActionRows() const;
    void ClearHotkey(const std::string& keys, uint8_t presses, const std::string& app, int exceptIndex);
    void HandleActionActivated(int rowIndex);
    void AddAction();
    void EditAction(int index, const Binding& seed);

    void SetupLogDisplay(HWND hWnd);
    void CleanupLogDisplay();

public:
    SettingsWindowViewModel(BaseWindow* baseView, AppConfig& config, std::function<void()> captureOverlayPosition,
                            std::function<void()> previewFont)
        : BaseViewModel(baseView), _cfg(config), _captureOverlayPosition(std::move(captureOverlayPosition)),
          _previewFont(std::move(previewFont)) {
    }

    ~SettingsWindowViewModel() override;

    /// Puts the frame's own pages in the settings registry. Once, at startup - a feature module
    /// adds its own the same way, from its own Register.
    static void RegisterPages();

    void Init() override;
};
