//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP
#include "BaseViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Core/BaseWindow.hpp"

class AudioManager;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    const AudioManager &_audioManager;
    const AppConfig& _config;

public:
    MainWindowViewModel(
        const std::shared_ptr<BaseWindow>& baseView,
        const AppConfig& config,
        const AudioManager& audioManager) :
            BaseViewModel(baseView),
            _audioManager(audioManager),
            _config(config) {

    }


};
#endif //EASYMIC_MAINWINDOWVIEWMODEL_HPP