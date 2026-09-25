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

    /// The single action waiting on a deadline. Held rather than queued so a newer one can take
    /// its place - see Defer.
    std::function<void()> _deferred;
    std::chrono::steady_clock::time_point _deferredAt{};

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

    /// Caller holds _mutex.
    void _enqueue(std::function<void()> action) {
        if (action) {
            _queue.push_back(std::move(action));
        }
    }

    /**
     * @brief Runs queued actions and closes the deferred window.
     *
     * The window is a timed wait, never a sleep: an action posted while one is open still runs the
     * moment it is queued. The predicate-less overload is deliberate - the one that takes a
     * predicate loops on the deadline it was given, so a Defer that moved the deadline out would
     * not be noticed until the original had passed.
     */
    void _loop() {
        std::unique_lock lock(_mutex);
        while (true) {
            if (_stop && _queue.empty()) {
                return;
            }

            if (!_queue.empty()) {
                auto action = std::move(_queue.front());
                _queue.pop_front();
                lock.unlock();
                _run(action);
                lock.lock();
                continue;
            }

            if (!_deferred) {
                _cv.wait(lock);
                continue;
            }

            _cv.wait_until(lock, _deferredAt);
            if (_deferred && std::chrono::steady_clock::now() >= _deferredAt) {
                _enqueue(std::move(_deferred));
                _deferred = nullptr;
            }
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
        _deferred = nullptr;
    }
    _worker = std::thread(_loop);
    return true;
}

void Stop() {
    {
        std::lock_guard lock(_mutex);
        _stop = true;
        _queue.clear();
        _deferred = nullptr;
    }
    _cv.notify_one();

    if (_worker.joinable()) {
        _worker.join();
    }
}

bool IsRunning() {
    return _worker.joinable();
}

void Post(std::function<void()> action) {
    if (!action) {
        return;
    }
    {
        std::lock_guard lock(_mutex);
        _enqueue(std::move(action));
    }
    _cv.notify_one();
}

void Defer(const std::chrono::steady_clock::time_point deadline, std::function<void()> action) {
    {
        std::lock_guard lock(_mutex);
        _deferred = std::move(action);
        _deferredAt = deadline;
    }
    _cv.notify_one();
}

void FlushDeferred() {
    {
        std::lock_guard lock(_mutex);
        if (!_deferred) {
            return;
        }
        _enqueue(std::move(_deferred));
        _deferred = nullptr;
    }
    _cv.notify_one();
}

void CancelDeferred() {
    std::lock_guard lock(_mutex);
    _deferred = nullptr;
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
