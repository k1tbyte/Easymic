//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_EVENT_HPP
#define EASYMIC_EVENT_HPP

#include <functional>
#include <mutex>
#include <vector>

/// Subscribe side only - owners hand this out so nobody but them can raise the event.
template<typename... Args>
class IEvent {
public:
    virtual ~IEvent() = default;
    virtual int operator+=(std::function<void(Args...)>) = 0;
    virtual void operator-=(int) = 0;
};

template<typename... Args>
class Event final : public IEvent<Args...> {
private:
    using Handler = std::function<void(Args...)>;

    struct Subscription {
        int id;
        Handler handler;
    };

    std::vector<Subscription> _subscriptions;
    mutable std::mutex _mutex;
    int _nextId = 0;

public:
    /// Subscribe - returns the id to unsubscribe with.
    int operator+=(Handler handler) override {
        std::lock_guard lock(_mutex);
        _subscriptions.push_back({_nextId, std::move(handler)});
        return _nextId++;
    }

    void operator-=(const int id) override {
        std::lock_guard lock(_mutex);
        std::erase_if(_subscriptions, [id](const Subscription& entry) { return entry.id == id; });
    }

    /// Handlers run under the lock: none of them subscribes to, unsubscribes from or destroys
    /// the event it is handling, and copying them out first would allocate on every raise -
    /// including the mute path and every log line.
    void operator()(Args... args) {
        std::lock_guard lock(_mutex);
        for (const Subscription& entry : _subscriptions) {
            entry.handler(args...);
        }
    }
};

#endif //EASYMIC_EVENT_HPP
