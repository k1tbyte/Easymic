#include "Dispatcher.hpp"

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

#include "definitions.h"

namespace Dispatcher {

namespace {

    std::thread _worker;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<std::function<void()>> _queue;
    bool _stop = false;

    /// An action is user code and may throw. Both places one runs sit under something that
    /// cannot take an exception - the worker thread's root, where one escaping calls
    /// std::terminate, and a window procedure, which would unwind through Win32.
    void _run(const std::function<void()>& action) {
        try {
            action();
        } catch (const std::exception& e) {
            LOG_ERROR("Dispatcher: action threw: %s", e.what());
        } catch (...) {
            LOG_ERROR("Dispatcher: action threw an unknown exception");
        }
    }

    void _loop() {
        std::unique_lock lock(_mutex);
        while (true) {
            _cv.wait(lock, [] { return _stop || !_queue.empty(); });
            // Stop dropped the queue
            if (_stop) {
                return;
            }
            auto action = std::move(_queue.front());
            _queue.pop_front();
            lock.unlock();
            _run(action);
            lock.lock();
        }
    }

    HWND _uiTarget = nullptr;

} // anonymous namespace

bool Start() {
    if (_worker.joinable()) {
        return false;
    }

    {
        // A hook can still post between Stop and its stage going quiet; that press is stale now
        std::lock_guard lock(_mutex);
        _stop = false;
        _queue.clear();
    }
    _worker = std::thread(_loop);
    return true;
}

void Stop() {
    {
        std::lock_guard lock(_mutex);
        _stop = true;
        _queue.clear();
    }
    _cv.notify_one();

    if (_worker.joinable()) {
        _worker.join();
    }
}

void Post(std::function<void()> action) {
    if (!action) {
        return;
    }
    {
        std::lock_guard lock(_mutex);
        _queue.push_back(std::move(action));
    }
    _cv.notify_one();
}

void BindUi(const HWND target) {
    _uiTarget = target;
}

void ToUi(std::function<void()> action) {
    if (!action || !_uiTarget) {
        return;
    }

    // Heap allocated because the action has to outlive this call and travel through the message
    // queue. RunPosted owns it from there; a post that fails frees it here instead.
    auto* posted = new std::function<void()>(std::move(action));
    if (!PostMessageW(_uiTarget, WM_DISPATCH_RUN, 0, reinterpret_cast<LPARAM>(posted))) {
        delete posted;
    }
}

void RunPosted(const LPARAM lParam) {
    const std::unique_ptr<std::function<void()>> posted(
        reinterpret_cast<std::function<void()>*>(lParam));
    if (posted && *posted) {
        _run(*posted);
    }
}

}
