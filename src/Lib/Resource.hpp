//
// Created by kitbyte on 10.11.2025.
//

#ifndef EASYMIC_RESOURCE_H
#define EASYMIC_RESOURCE_H

#include <windows.h>
#include <memory>

// RAII Resource wrapper for automatic memory management
class Resource {
private:
    std::unique_ptr<BYTE[]> _ownedBuffer;  // For file-loaded resources
    BYTE* _rawBuffer = nullptr;            // For embedded resources
    DWORD _fileSize = 0;
    bool _ownsMemory = false;

public:
    Resource() = default;

    // Constructor for file-loaded resources (owns memory)
    Resource(std::unique_ptr<BYTE[]> buffer, DWORD size)
        : _ownedBuffer(std::move(buffer)), _fileSize(size), _ownsMemory(true) {}

    // Constructor for embedded resources (doesn't own memory)
    Resource(BYTE* buffer, DWORD size)
        : _rawBuffer(buffer), _fileSize(size), _ownsMemory(false) {}

    BYTE* buffer() const {
        return _ownsMemory ? _ownedBuffer.get() : _rawBuffer;
    }

    DWORD fileSize() const { return _fileSize; }

    bool empty() const {
        return (_ownsMemory ? _ownedBuffer == nullptr : _rawBuffer == nullptr) || _fileSize == 0;
    }

    // Move constructor and assignment
    Resource(Resource&& other) noexcept
        : _ownedBuffer(std::move(other._ownedBuffer)),
          _rawBuffer(other._rawBuffer),
          _fileSize(other._fileSize),
          _ownsMemory(other._ownsMemory) {
        other._rawBuffer = nullptr;
        other._fileSize = 0;
        other._ownsMemory = false;
    }

    Resource& operator=(Resource&& other) noexcept {
        if (this != &other) {
            _ownedBuffer = std::move(other._ownedBuffer);
            _rawBuffer = other._rawBuffer;
            _fileSize = other._fileSize;
            _ownsMemory = other._ownsMemory;

            other._rawBuffer = nullptr;
            other._fileSize = 0;
            other._ownsMemory = false;
        }
        return *this;
    }

    // Delete copy operations
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};
#endif //EASYMIC_RESOURCE_H