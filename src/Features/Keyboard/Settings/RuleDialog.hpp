#pragma once

#include <windows.h>

struct WordRule;

namespace RuleDialog {
    bool Show(HWND owner, WordRule& rule);
}
