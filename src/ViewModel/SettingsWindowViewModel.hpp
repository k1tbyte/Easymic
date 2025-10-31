//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#define EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
#include "AudioManager.hpp"
#include "BaseViewModel.hpp"
#include "SettingsWindow/SettingsWindow.hpp"
#include "View/Core/BaseWindow.hpp"

class SettingsWindowViewModel final : public BaseViewModel<SettingsWindow> {
private:
    const AudioManager &_audioManager;

public:
    SettingsWindowViewModel(const std::shared_ptr<BaseWindow>& baseView, const AudioManager& audioManager) :
        BaseViewModel(baseView),
        _audioManager(audioManager) {

    }
};
#endif //EASYMIC_SETTINGSWINDOWVIEWMODEL_HPP
