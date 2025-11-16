//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#define EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP

#include "AudioManager.hpp"
#include "Audio/AudioFileValidator.hpp"
#include "Utils.hpp"
#include "ViewModel.hpp"
#include "SettingsWindow/SettingsWindow.hpp"
#include "View/Core/BaseWindow.hpp"
#include "AppConfig.hpp"
#include "MainWindow/MainWindow.hpp"
#include "Lib/Logger.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    const AudioManager &_audioManager;
    AppConfig& _cfg;
    AppConfig _cfgPrev;
    MainWindow* MainWnd = nullptr;
    
    // Logger UI management
    int logAddedSubscriptionId_ = -1;
    int logClearedSubscriptionId_ = -1;
    HWND currentAboutHwnd_ = nullptr;

    constexpr static const char* IndicatorStates[] = {
        "Hidden", "Muted", "Muted or talking"
    };

    // UI Helper Methods
    void InitializeGeneralSection(HWND hWnd);
    void InitializeIndicatorSection(HWND hWnd);
    void InitializeSoundsSection(HWND hWnd);
    void InitializeHotkeysSection(HWND hWnd);
    void InitializeAboutSection(HWND hWnd);
    
    void SetupLogDisplay(HWND hWnd);
    void CleanupLogDisplay();
    void UpdateLogDisplay(const std::string& formattedEntry);
    
    // Sound file handling
    bool SelectSoundFile(HWND hWnd, const char* title, std::string& result);
    void UpdateSoundComboBox(HWND hWnd, int comboBoxId, const std::set<std::string>& sources, const std::string& current);
    void HandleSoundSourceSelection(HWND hWnd, int comboBoxId, std::string& configSource, std::set<std::string>& recentSources);
    bool HandleSoundFileBrowse(HWND hWnd, int comboBoxId, const char* title, std::string& configSource, std::set<std::string>& recentSources);

public:
    SettingsWindowViewModel(const std::shared_ptr<BaseWindow>& baseView, AppConfig& config, const AudioManager& audioManager) :
        BaseViewModel(baseView),
        _audioManager(audioManager),
        _cfg(config) {
    }
    
    ~SettingsWindowViewModel() {
        CleanupLogDisplay();
    }

    void HandleSectionChange(HWND hWnd, int sectionId);
    void HandleButtonClick(HWND hWnd, int buttonId);
    void HandleComboBoxChange(HWND hWnd, int comboBoxId);
    void HandleTrackbarChange(HWND hWnd, int trackbarId, int value);
    void HandleHotkeyBinding(HWND hWnd, int index, LPCSTR itemText) const;
    
    void Init() override;
};

#endif //EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
