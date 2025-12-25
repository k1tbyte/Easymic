//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMIC_EVENT_HPP
#define EASYMIC_EVENT_HPP

#include <functional>
#include <unordered_map>

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
    std::unordered_map<int, Handler> handlers_;
    int nextId_ = 0;

public:
    // Subscribe - returns ID
    int operator+=(Handler handler) override {
        int id = nextId_++;
        handlers_[id] = std::move(handler);
        return id;
    }

    // Unsubscribe by ID
    void operator-=(int id) override {
        handlers_.erase(id);
    }

    void operator () (Args... args) {
        invoke(args...);
    }

    void invoke(Args... args) {
        for (auto& [id, handler] : handlers_) {
            handler(args...);
        }
    }

    void clear() {
        handlers_.clear();
    }
};

#endif //EASYMIC_EVENT_HPP