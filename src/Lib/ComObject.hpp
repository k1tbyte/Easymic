#pragma once

#include <windows.h>

/**
 * @brief Reference counting for the WASAPI callback objects, once instead of per handler.
 *
 * Derives from the interface so AddRef and Release land in its vtable slots - a separate base
 * would leave the COM ones pure. QueryInterface stays with each handler: the interface list is
 * the one part that genuinely differs between them.
 *
 * Lifetime is the refcount, so these are always heap allocated and never deleted by hand.
 */
template<typename Interface>
class ComObject : public Interface {
    LONG _refCount = 1;

public:
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = InterlockedDecrement(&_refCount);
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

protected:
    /// Virtual so the delete above runs the handler's destructor and not just this one.
    virtual ~ComObject() = default;
};

