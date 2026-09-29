#pragma once

#include <functional>
#include <mutex>
#include <vector>

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
    int operator+=(Handler handler) override {
        std::lock_guard lock(_mutex);
        _subscriptions.push_back({_nextId, std::move(handler)});
        return _nextId++;
    }

    void operator-=(const int id) override {
        std::lock_guard lock(_mutex);
        std::erase_if(_subscriptions, [id](const Subscription& entry) { return entry.id == id; });
    }

    /// Run under the lock: a handler must not (un)subscribe or destroy the event; copying them out allocates per raise.
    void operator()(Args... args) {
        std::lock_guard lock(_mutex);
        for (const Subscription& entry : _subscriptions) {
            entry.handler(args...);
        }
    }
};

