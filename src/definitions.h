//
// Created by kitbyte on 24.10.2025.
//

#ifndef EASYMICTESTING_PCH_H
#define EASYMICTESTING_PCH_H

#include <windows.h>
#include <wrl/client.h>


using Microsoft::WRL::ComPtr;


#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        printf("%s FAILED: 0x%08lX\n", msg, hr); \
        throw std::runtime_error(msg); \
    }

#endif //EASYMICTESTING_PCH_H