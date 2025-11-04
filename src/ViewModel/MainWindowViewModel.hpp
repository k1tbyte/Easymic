//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP


#include "SettingsWindowViewModel.hpp"
#include "../Lib/UIAccess/UIAccessManager.hpp"
#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Core/BaseWindow.hpp"

#include "Utils.hpp"

class AudioManager;

using namespace Gdiplus;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    constexpr static const char* SHADOW_WINDOW_KEY = "EasymicTopLevel";

    std::shared_ptr<SettingsWindow> _settingsWindow;

    const AudioManager &_audio;
    AppConfig& _cfg;
    HICON mutedIcon = nullptr;
    HICON unmutedIcon = nullptr;
    HICON activeIcon = nullptr;

    HICON iconToDisplay = nullptr;

    Resource muteSound;
    Resource unmuteSound;

    bool hasRenderDevice = false;
    bool renderDeviceMuted = false;
    float volumeLevel = 0;

public:
    MainWindowViewModel(
        const std::shared_ptr<BaseWindow>& baseView,
        AppConfig& config,
        const AudioManager& audioManager) :
            BaseViewModel(baseView),
            _audio(audioManager),
            _cfg(config) {
    }

private:

    void SuspendActivity() {
        _view->Hide();
        _view->SetShadowHwnd(nullptr);
    }

    void RestoreConfig() {

        if (_cfg.onTopExclusive && !_view->IsOvershadowed()) {
            _view->Hide();
            auto *shadowHwnd = UIAccessManager::GetOrCreateWindow(SHADOW_WINDOW_KEY, MainWindow::StyleEx, MainWindow::Style);
            _view->SetShadowHwnd(shadowHwnd);
            _view->RefreshPos(HWND_TOPMOST);
            //Hide shadow window
            _view->Hide();
        }

        /*SetWindowDisplayAffinity(_view->GetHandle(),
                         _cfg.excludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);*/

        if (_cfg.indicator == IndicatorState::Hidden) {
            _view->Hide();
            return;
        }

        _view->Show();
    }


    void OnTrayMenuCommand(UINT_PTR commandId) {
        switch (commandId) {
            case ID_APP_EXIT:
                PostQuitMessage(0);
                break;
            case ID_APP_SETTINGS:

                if (_settingsWindow) {
                    return;
                }

                SuspendActivity();
                _settingsWindow = std::make_shared<SettingsWindow>(_view->GetHInstance());
                _settingsWindow->AttachViewModel<SettingsWindowViewModel>(_cfg, _audio);

                if (!_settingsWindow->Initialize({ .parentHwnd =  _view->GetHandle() })) {
                    throw std::runtime_error("Failed to initialize settings window");
                }

                _settingsWindow->OnExit += [this] {
                    RestoreConfig();
                    _settingsWindow.reset();
                };
                _settingsWindow->Show();
                break;
        }
    }

    void OnRender(const RenderContext& ctx) {
#define WND_BACKGROUND RGB(24,27,40)
        GraphicsPath path;
        RectF rect(0, 0, static_cast<float>(ctx.width), static_cast<float>(ctx.height));

        GDIRenderer::CreateRoundedRectPath(path,rect,10.0f);
        SolidBrush brush(Color(220, GetRValue(WND_BACKGROUND), GetGValue(WND_BACKGROUND), GetBValue(WND_BACKGROUND)));
        ctx.graphics->FillPath(&brush, &path);

        Bitmap iconBitmap(iconToDisplay);
        const auto iconSize = _cfg.indicatorSize;

        ctx.graphics->DrawImage(&iconBitmap,
            ctx.width / 2 - iconSize / 2,
            ctx.width / 2 - iconSize / 2,
            iconSize,
            iconSize
        );
    }

    void RenderDeviceStateChanged(bool silent) {
        const auto& mic = *_audio.CaptureDevice();

        if (!hasRenderDevice) {
            _view->UpdateTrayTooltip(L"Easymic - No device");
            iconToDisplay = nullptr;
            _view->UpdateTrayIcon(mutedIcon);
            return;
        }

        _view->UpdateTrayIcon(iconToDisplay = (renderDeviceMuted ?  mutedIcon : unmutedIcon));

        constexpr auto bufferSize = 255;

        wchar_t buffer[bufferSize];
        swprintf(buffer, bufferSize, L"Easymic - %ls [%d%%]",
                 mic.GetDeviceName(),
                 mic.GetVolumePercent());
        _view->UpdateTrayTooltip(std::wstring(buffer));

        if (_cfg.bellVolume > 0 && !silent) {
            PlaySoundA(
                renderDeviceMuted ? (LPCSTR)muteSound.buffer : (LPCSTR)unmuteSound.buffer,
                nullptr,
                SND_ASYNC | SND_MEMORY
            );
            _view->Invalidate();
        }
    }

    void UpdateDevice() {
        const auto& mic = *_audio.CaptureDevice();
        hasRenderDevice = mic.IsInitialized();
        renderDeviceMuted = mic.IsMuted();

        RenderDeviceStateChanged(true);
    }

public:
    void Init() override {
        auto *hInst = _view->GetHInstance();

        mutedIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_MUTED));
        unmutedIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_UNMUTED));
        activeIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MIC_ACTIVE));

        Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_UNMUTE), "WAVE",
                 &unmuteSound.buffer, &unmuteSound.fileSize);

        Utils::LoadResource(hInst, MAKEINTRESOURCE(IDR_MUTE), "WAVE",
                 &muteSound.buffer, &muteSound.fileSize);

        _audio.OnCaptureStateChanged += [this](bool muted, float level) {
            bool silent = renderDeviceMuted == muted;
            renderDeviceMuted = muted;
            this->RenderDeviceStateChanged(silent);
        };

        _audio.OnDefaultCaptureChanged += [this]() {
            this->UpdateDevice();
        };

        _view->CreateTrayIcon(nullptr, L"");

        _view->SetOnTrayMenu([this](UINT_PTR commandId) {
            OnTrayMenuCommand(commandId);
        });

        _view->SetRenderCallback([this](const RenderContext& context) {
            this->OnRender(context);
        });

        UpdateDevice();
        RestoreConfig();
    }

    ~MainWindowViewModel() {
        if (mutedIcon) {
            DestroyIcon(mutedIcon);
        }
        if (unmutedIcon) {
            DestroyIcon(unmutedIcon);
        }
    }
};
#endif //EASYMIC_MAINWINDOWVIEWMODEL_HPP