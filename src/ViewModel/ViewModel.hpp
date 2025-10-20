//
// Created by kitbyte on 31.10.2025.
//

#ifndef EASYMIC_BASEVIEWMODEL_HPP
#define EASYMIC_BASEVIEWMODEL_HPP

#include <memory>

class BaseWindow;

// Interface for polymorphic storage in BaseWindow
class IViewModel {
public:
    virtual ~IViewModel() = default;
    virtual void Init() = 0; // Must be implemented by concrete ViewModels
};

// Template base class for typed view access
template <typename T>
class BaseViewModel : public IViewModel {
protected:
    std::shared_ptr<T> _view;

public:
    explicit BaseViewModel(const std::shared_ptr<BaseWindow>& view)
        : _view(std::static_pointer_cast<T>(view)) {
    }

    std::shared_ptr<T> GetView() const { return _view; }
};
#endif //EASYMIC_BASEVIEWMODEL_HPP