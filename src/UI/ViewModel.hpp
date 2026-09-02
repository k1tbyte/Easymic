//
// Created by kitbyte on 31.10.2025.
//

#pragma once

class BaseWindow;

// Interface for polymorphic storage in BaseWindow
class IViewModel {
public:
    virtual ~IViewModel() = default;
    virtual void Init() = 0; // Must be implemented by concrete ViewModels
};

/**
 * @brief Template base class for typed view access.
 *
 * The window owns the view model, so the back pointer is raw on purpose: a shared_ptr here
 * would close a cycle and neither side would ever be destroyed.
 */
template <typename T>
class BaseViewModel : public IViewModel {
protected:
    T* _view;

public:
    explicit BaseViewModel(BaseWindow* view) : _view(static_cast<T*>(view)) {
    }
};
