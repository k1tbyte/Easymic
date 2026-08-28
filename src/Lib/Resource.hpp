#pragma once

#include <memory>
#include <string>
#include <windows.h>

/// Either a heap buffer we own (file) or a pointer into the module image (embedded resource).
class Resource {
    std::unique_ptr<BYTE[]> _owned;
    BYTE* _buffer = nullptr;
    DWORD _size = 0;

public:
    Resource() = default;

    Resource(std::unique_ptr<BYTE[]> buffer, DWORD size)
        : _owned(std::move(buffer)), _buffer(_owned.get()), _size(size) {}

    Resource(BYTE* buffer, DWORD size)
        : _buffer(buffer), _size(size) {}

    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    BYTE* buffer() const { return _buffer; }
    DWORD fileSize() const { return _size; }
    bool empty() const { return !_buffer || !_size; }

    /// Embedded resource - the module image owns the memory, so nothing is copied.
    static Resource FromModule(HINSTANCE hInst, LPCSTR name, LPCSTR type) {
        HRSRC info = FindResourceA(hInst, name, type);
        if (!info) {
            return {};
        }

        HGLOBAL handle = ::LoadResource(hInst, info);
        if (!handle) {
            return {};
        }

        return {static_cast<BYTE*>(LockResource(handle)), SizeofResource(hInst, info)};
    }

};

