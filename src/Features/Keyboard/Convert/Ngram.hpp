#pragma once

#include <cstdint>
#include <string>

namespace Convert {

    double NgramScore(const std::wstring& symbols, const uint32_t* tri,
                      const uint32_t* bi, const std::wstring& text);

}
