//
// Created by kitbyte on 31.10.2025.
//

#pragma once

#include "Features/Microphone/Wasapi/AudioManager.hpp"
#include "Core/ActionRegistry.hpp"
#include "AudioFileValidator.hpp"
#include "Registry.hpp"
#include "Settings/DialogControls.hpp"
#include "ViewModel.hpp"
#include "Settings/SettingsWindow.hpp"
#include "BaseWindow.hpp"
#include "AppConfig.hpp"
#include "MainWindow.hpp"
#include "Logger.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    const AudioManager &_audioManager;
    AppConfig& _cfg;
    AppConfig _cfgPrev;
    MainWindow* _mainWindow = nullptr;

    /// Autostart lives in the registry, not the config, so the pending state is tracked here and
    /// written on Apply - closing with Cancel must leave the machine exactly as it was.
    bool _autoStartRequested = false;
    bool _autoStartInitial = false;

    // Logger UI management
    int _logAddedSubscriptionId = -1;
    HFONT _linkFont = nullptr;

    constexpr static const wchar_t* IndicatorStates[] = {
        L"Hidden", L"Muted", L"Muted or talking"
    };

    // UI Helper Methods
    void InitializeGeneralSection(HWND hWnd) const;
    void InitializeHotkeysSection(HWND hWnd) const;
    void InitializeIndicatorSection(HWND hWnd) const;
    void InitializeSoundsSection(HWND hWnd) const;
    void RefreshActionRows() const;
    void ClearHotkey(const std::string& keys, uint8_t presses, int exceptIndex);
    void AddAction();
    void EditAction(int index, const Binding& seed);
    void InitializeAboutSection(HWND hWnd);

    void SetupLogDisplay(HWND hWnd);
    void CleanupLogDisplay();

    /// Offers to restart elevated. @return true when the app is on its way out and the caller
    /// must not touch anything else.
    bool RequestElevationFor(HWND hWnd, const wchar_t* feature) const;
    void CommitPrivilegedSettings() const;

    // Mic state feedback sounds - the action sounds live in the action dialog instead
    void HandleSoundSelection(HWND hWnd, int comboBoxId, std::string& configSource) const;
    void HandleSoundBrowse(HWND hWnd, int comboBoxId, const char* title, std::string& configSource) const;

public:
    SettingsWindowViewModel(BaseWindow* baseView, AppConfig& config, const AudioManager& audioManager)
        : BaseViewModel(baseView), _audioManager(audioManager), _cfg(config) {
    }

    ~SettingsWindowViewModel() override {
        CleanupLogDisplay();
        if (_linkFont) {
            DeleteObject(_linkFont);
        }
    }

    void HandleSectionChange(HWND hWnd, int sectionId);
    void HandleButtonClick(HWND hWnd, int buttonId);
    void HandleComboBoxChange(HWND hWnd, int comboBoxId) const;
    void HandleTrackbarChange(HWND hWnd, int trackbarId, int value) const;
    void HandleActionActivated(int rowIndex);

    void Init() override;
};

