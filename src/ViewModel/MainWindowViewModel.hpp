//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_MAINWINDOWVIEWMODEL_HPP
#define EASYMIC_MAINWINDOWVIEWMODEL_HPP
#include <format>

#include "ViewModel.hpp"
#include "MainWindow/MainWindow.hpp"
#include "View/Core/BaseWindow.hpp"

#include "Utils.hpp"

class AudioManager;

using namespace Gdiplus;

class MainWindowViewModel final : public BaseViewModel<MainWindow> {
private:
    const AudioManager &_audio;
    const AppConfig& _config;
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
        const AppConfig& config,
        const AudioManager& audioManager) :
            BaseViewModel(baseView),
            _audio(audioManager),
            _config(config) {
    }

    void OnTrayMenuCommand(UINT_PTR commandId) {
        switch (commandId) {
            case ID_APP_EXIT:
                PostQuitMessage(0);
                break;
            case ID_APP_SETTINGS:
                // Show settings window
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
        const auto iconSize = _config.indicatorSize;

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

        _view->UpdateTrayTooltip(std::format(L"Easymic - {} [{}%]", mic.GetDeviceName(), mic.GetVolumePercent()));

        if (_config.bellVolume > 0 && !silent) {
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