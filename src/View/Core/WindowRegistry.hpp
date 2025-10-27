#ifndef EASYMIC_WINDOWREGISTRY_HPP
#define EASYMIC_WINDOWREGISTRY_HPP

#include <windows.h>
#include <unordered_map>
#include <mutex>

// Forward declaration
class BaseWindow;

/**
 * @brief Singleton registry для маппинга HWND -> BaseWindow*
 * Решает проблему глобальных статических указателей
 */
class WindowRegistry {
public:
    static WindowRegistry& Instance() {
        static WindowRegistry instance;
        return instance;
    }

    void Register(HWND hwnd, BaseWindow* window) {
        std::lock_guard lock(mutex_);
        registry_[hwnd] = window;
    }

    void Unregister(HWND hwnd) {
        std::lock_guard lock(mutex_);
        registry_.erase(hwnd);
    }

    BaseWindow* Get(HWND hwnd) {
        std::lock_guard lock(mutex_);
        auto it = registry_.find(hwnd);
        return it != registry_.end() ? it->second : nullptr;
    }

    WindowRegistry(const WindowRegistry&) = delete;
    WindowRegistry& operator=(const WindowRegistry&) = delete;

private:
    WindowRegistry() = default;
    ~WindowRegistry() = default;

    std::unordered_map<HWND, BaseWindow*> registry_;
    std::mutex mutex_;
};

#endif //EASYMIC_WINDOWREGISTRY_HPP

