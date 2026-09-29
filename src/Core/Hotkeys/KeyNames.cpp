#include "KeyNames.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>

#include "KeyChord.hpp"

namespace {

struct KeyName {
    uint8_t Vk;
    const char* Name;
};

constexpr KeyName Named[] = {
    {VK_LBUTTON, "MouseLeft"}, {VK_RBUTTON, "MouseRight"}, {VK_CANCEL, "Cancel"},
    {VK_MBUTTON, "MouseMiddle"}, {VK_XBUTTON1, "MouseX1"}, {VK_XBUTTON2, "MouseX2"},
    {VK_BACK, "Back"}, {VK_TAB, "Tab"}, {VK_CLEAR, "Clear"}, {VK_RETURN, "Enter"},
    {VK_PAUSE, "Pause"}, {VK_CAPITAL, "CapsLock"}, {VK_ESCAPE, "Escape"}, {VK_CONVERT, "Convert"},
    {VK_NONCONVERT, "NonConvert"}, {VK_ACCEPT, "Accept"}, {VK_MODECHANGE, "ModeChange"},
    {VK_SPACE, "Space"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_END, "End"},
    {VK_HOME, "Home"}, {VK_LEFT, "Left"}, {VK_UP, "Up"}, {VK_RIGHT, "Right"}, {VK_DOWN, "Down"},
    {VK_SELECT, "Select"}, {VK_PRINT, "Print"}, {VK_EXECUTE, "Execute"},
    {VK_SNAPSHOT, "PrintScreen"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HELP, "Help"},
    {VK_LWIN, "LWin"}, {VK_RWIN, "RWin"}, {VK_APPS, "Apps"}, {VK_SLEEP, "Sleep"},
    {VK_MULTIPLY, "Multiply"}, {VK_ADD, "Add"}, {VK_SEPARATOR, "Separator"},
    {VK_SUBTRACT, "Subtract"}, {VK_DECIMAL, "Decimal"}, {VK_DIVIDE, "Divide"},
    {VK_NUMLOCK, "NumLock"}, {VK_SCROLL, "ScrollLock"}, {VK_BROWSER_BACK, "BrowserBack"},
    {VK_BROWSER_FORWARD, "BrowserForward"}, {VK_BROWSER_REFRESH, "BrowserRefresh"},
    {VK_BROWSER_STOP, "BrowserStop"}, {VK_BROWSER_SEARCH, "BrowserSearch"},
    {VK_BROWSER_FAVORITES, "BrowserFavorites"}, {VK_BROWSER_HOME, "BrowserHome"},
    {VK_VOLUME_MUTE, "VolumeMute"}, {VK_VOLUME_DOWN, "VolumeDown"}, {VK_VOLUME_UP, "VolumeUp"},
    {VK_MEDIA_NEXT_TRACK, "MediaNextTrack"}, {VK_MEDIA_PREV_TRACK, "MediaPrevTrack"},
    {VK_MEDIA_STOP, "MediaStop"}, {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"},
    {VK_LAUNCH_MAIL, "LaunchMail"}, {VK_LAUNCH_MEDIA_SELECT, "LaunchMediaSelect"},
    {VK_LAUNCH_APP1, "LaunchApp1"}, {VK_LAUNCH_APP2, "LaunchApp2"}, {VK_OEM_1, ":"},
    {VK_OEM_PLUS, "Plus"}, {VK_OEM_COMMA, ","}, {VK_OEM_MINUS, "-"}, {VK_OEM_PERIOD, "."},
    {VK_OEM_2, "/?"}, {VK_OEM_3, "~"}, {VK_OEM_4, "["}, {VK_OEM_5, "\\"}, {VK_OEM_6, "]"},
    {VK_OEM_7, "\""}, {VK_OEM_8, "OEM_8"}, {VK_OEM_102, "<"},
};

struct Label {
    char Text[8]{};
};

constexpr Label MakeLabel(const std::string_view prefix, const int number) {
    Label label;
    size_t length = 0;
    for (const char c : prefix) {
        label.Text[length++] = c;
    }
    if (number >= 10) {
        label.Text[length++] = static_cast<char>('0' + number / 10);
    }
    label.Text[length] = static_cast<char>('0' + number % 10);
    return label;
}

template <size_t Count>
constexpr std::array<Label, Count> MakeLabels(const std::string_view prefix, const int first) {
    std::array<Label, Count> labels;
    for (size_t i = 0; i < Count; ++i) {
        labels[i] = MakeLabel(prefix, first + static_cast<int>(i));
    }
    return labels;
}

constexpr auto Digits = MakeLabels<10>("", 0);
constexpr auto NumPad = MakeLabels<10>("NumPad", 0);
constexpr auto FunctionKeys = MakeLabels<24>("F", 1);
constexpr auto Letters = [] {
    std::array<Label, 26> letters;
    for (size_t i = 0; i < letters.size(); ++i) {
        letters[i].Text[0] = static_cast<char>('A' + i);
    }
    return letters;
}();

template <size_t Count>
constexpr void Assign(std::array<const char*, 256>& table, const uint8_t first, const std::array<Label, Count>& labels) {
    for (size_t i = 0; i < Count; ++i) {
        table[first + i] = labels[i].Text;
    }
}

constexpr std::array<const char*, 256> KeysNameTable = [] {
    std::array<const char*, 256> table{};
    for (const KeyName& key : Named) {
        table[key.Vk] = key.Name;
    }
    Assign(table, '0', Digits);
    Assign(table, 'A', Letters);
    Assign(table, VK_NUMPAD0, NumPad);
    Assign(table, VK_F1, FunctionKeys);
    return table;
}();

bool EqualsIgnoreCase(const std::string_view left, const std::string_view right) {
    constexpr auto lower = [](const char c) { return std::tolower(static_cast<unsigned char>(c)); };
    return std::ranges::equal(left, right, {}, lower, lower);
}

uint8_t ModifierBit(const std::string_view token) {
    for (const Modifier& modifier : Modifiers) {
        if (EqualsIgnoreCase(token, modifier.Name) || (modifier.Alias && EqualsIgnoreCase(token, modifier.Alias))) {
            return modifier.Bit;
        }
    }
    return 0;
}

/// Hex is what Format prints for a key with no name, so it is read back here.
int KeyCode(const std::string_view token) {
    if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
        unsigned value = 0;
        const char* const last = token.data() + token.size();
        if (const auto [ptr, ec] = std::from_chars(token.data() + 2, last, value, 16);
            ec == std::errc{} && ptr == last && value && value <= 0xFF) {
            return static_cast<int>(value);
        }
        return -1;
    }

    for (int code = 1; code < 256; code++) {
        if (KeysNameTable[code] && EqualsIgnoreCase(token, KeysNameTable[code])) {
            return code;
        }
    }
    return -1;
}

std::string_view Trim(std::string_view token) {
    while (!token.empty() && token.front() == ' ') {
        token.remove_prefix(1);
    }
    while (!token.empty() && token.back() == ' ') {
        token.remove_suffix(1);
    }
    return token;
}

} // anonymous namespace

namespace KeyNames {

std::string Format(const uint64_t keysMask) {
    std::string result;
    const auto append = [&result](const std::string_view name) {
        if (!result.empty()) {
            result += " + ";
        }
        result += name;
    };

    for (const Modifier& modifier : Modifiers) {
        if (keysMask & modifier.Bit) {
            append(modifier.Name);
        }
    }
    for (const uint8_t vk : KeysOf(keysMask)) {
        if (const char* name = KeysNameTable[vk]) {
            append(name);
            continue;
        }
        char hexCode[7];
        sprintf_s(hexCode, "0x%02X", vk);
        append(hexCode);
    }
    return result;
}

uint64_t Parse(const std::string_view name) {
    KeyChord chord;

    for (size_t start = 0; start <= name.size();) {
        const size_t plus = name.find('+', start);
        const size_t end = plus == std::string_view::npos ? name.size() : plus;
        const std::string_view token = Trim(name.substr(start, end - start));
        start = end + 1;

        if (token.empty()) {
            continue;
        }

        if (const uint8_t bit = ModifierBit(token)) {
            chord.Mask |= bit;
            continue;
        }

        // A key named twice would take two bytes of the mask, and the hook only ever writes it
        // to one - the combination could never match
        const int code = KeyCode(token);
        const KeyList held = KeysOf(chord.Mask);
        if (code < 0 || held.Count == held.Vk.size() || std::ranges::contains(held, static_cast<uint8_t>(code))) {
            return 0;
        }
        chord.Push(static_cast<uint8_t>(code));
    }
    return chord.Mask;
}

}
