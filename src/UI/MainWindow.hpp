#pragma once

#include <memory>
#include <string>

#include "AppConfig.hpp"
#include "BaseWindow.hpp"
#include "Core/Feedback.hpp"
#include "LayeredWindow.hpp"
#include "Overlay/OverlaySurface.hpp"
#include "Settings/SettingsWindow.hpp"
#include "TrayIcon.hpp"

/// The frame: the overlay's window, the tray icon, the settings window, and putting the config back
/// into effect when that window closes. What the overlay and the tray show belongs to whoever
/// registered into them - this only asks.
class MainWindow final : public BaseWindow {
public:
    struct WindowConfig {
        LPCWSTR windowTitle = L"MainWindow";
        LPCWSTR className = L"MainWindowClass";
    };

    MainWindow(HINSTANCE hInstance, AppConfig& config, Feedback& feedback);
    ~MainWindow() override = default;

    bool Initialize(WindowConfig config);

    /**
     * @brief Hands a notification text to the window from any thread.
     *
     * The text travels with the message, so nothing is shared and nothing races. Takes no owner
     * state: a captured command may answer long after its action is gone, and a window that no
     * longer exists only costs a failed post.
     */
    static void PostNotification(const std::wstring& text);

    /// Asks the overlay to measure itself again, from any thread. Carries nothing but the
    /// request: whatever changed, the layer that changed it already knows.
    static void PostRelayout();

    /// Asks for the tray icon and tooltip to be read again, from any thread.
    static void PostTrayRefresh();

private:
    bool RegisterWindowClass(const WindowConfig& config) const;

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    LRESULT OnDestroy();
    LRESULT OnPaint();
    LRESULT OnTrayIconMessage(LPARAM lParam);

    void ShowTrayContextMenu();
    bool CreateTrayIcon(HICON icon, const std::wstring& tooltip);
    /// The provider the config names paints the icon, and says the tooltip.
    void RefreshTray();

    void SuspendActivity();
    void RestoreConfig();
    void OpenSettings();
    void ToggleInteractivity(bool interactive) const;

    AppConfig& _cfg;
    Feedback& _feedback;
    OverlaySurface _overlay;
    std::unique_ptr<SettingsWindow> _settings;

    TrayIcon _trayIcon;
    LayeredWindow::Surface _surface;

    // Not owned: LoadIcon returns shared icons, the tray provider keeps them alive
    HICON _currentIcon = nullptr;
    std::wstring _currentTooltip;

    /// Registered at runtime, so it cannot be a switch label - checked before the switch.
    UINT _taskbarCreatedMessage = 0;

    /// Where the two posters send. One copy of the handle rather than one per caller, and it is
    /// deliberately never cleared: posting to a destroyed window fails harmlessly, while posting
    /// to a null one would queue a thread message nobody dispatches.
    static inline HWND _postTarget = nullptr;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    /// WM_APP, not WM_USER: the timer ids the overlay owns share nothing but the number line
    static constexpr UINT WM_SHOW_NOTIFICATION = WM_APP + 1;
    static constexpr UINT WM_OVERLAY_RELAYOUT = WM_APP + 2;
    static constexpr UINT WM_TRAY_REFRESH = WM_APP + 3;
    /// A menu contribution's command id is this plus its provider's index - not a resource id.
    static constexpr UINT TrayMenuFirst = 41000;
};
