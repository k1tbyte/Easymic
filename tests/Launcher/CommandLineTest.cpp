#include <cstdio>
#include <string>

#include "../Check.hpp"
#include "Features/Launcher/CommandLine.hpp"

namespace {

    using Test::Check;
    using namespace CommandLine;

    bool _nothingExists(const std::wstring&) { return false; }
    bool _everythingExists(const std::wstring&) { return true; }

    void _split() {
        Parts parts = Split(LR"("C:\Program Files\x.exe" -a b)", _nothingExists);
        Check(parts.File == LR"(C:\Program Files\x.exe)" && parts.Params == L"-a b", "quoted target keeps its spaces");

        parts = Split(LR"(C:\Program Files\x.exe)", _everythingExists);
        Check(parts.File == LR"(C:\Program Files\x.exe)" && parts.Params.empty(), "a line that is a path is all target");

        parts = Split(L"notepad   a.txt b.txt", _nothingExists);
        Check(parts.File == L"notepad" && parts.Params == L"a.txt b.txt", "first token is the target, spaces between are skipped");

        parts = Split(L"notepad", _nothingExists);
        Check(parts.File == L"notepad" && parts.Params.empty(), "no arguments");

        parts = Split(L"\"C:\\a b\\x.exe", _nothingExists);
        Check(parts.File == L"C:\\a b\\x.exe" && parts.Params.empty(), "unterminated quote takes the rest");

        parts = Split(L"\"x.exe\"", _nothingExists);
        Check(parts.File == L"x.exe" && parts.Params.empty(), "quoted target without arguments");
    }

    void _quote() {
        Check(Quote("C:\\Work", false) == "C:\\Work", "a plain value is left alone");
        Check(Quote("C:\\My Work", false) == "\"C:\\My Work\"", "a space is quoted");
        for (const char* special : {"C:\\R&D", "a^b", "50%", "(x)", "a,b", "a;b", "a=b", "wow!"}) {
            const std::string quoted = Quote(special, false);
            Check(quoted == std::string("\"") + special + "\"", "a cmd metacharacter is quoted");
        }
        Check(Quote("C:\\My Work\\", false) == "\"C:\\My Work\\\\\"", "a trailing backslash is doubled before the closing quote");
        Check(Quote("C:\\My Work", true) == "C:\\My Work", "already inside quotes: bare");
        Check(Quote("C:\\My Work\\", true) == "C:\\My Work\\\\", "already inside quotes: still doubled");
        Check(Quote("", false).empty(), "empty stays empty");
    }

    void _expandDir() {
        Check(ExpandDir("wt -d {dir}", "C:\\R&D") == "wt -d \"C:\\R&D\"", "token is replaced by a quoted folder");
        Check(ExpandDir("wt -d \"{dir}\"", "C:\\My Work\\") == "wt -d \"C:\\My Work\\\\\"", "quoted token gets no second pair");
        Check(ExpandDir("a {dir} b {dir}", "C:\\x") == "a C:\\x b C:\\x", "every occurrence");
        Check(ExpandDir("a {dir}", "{dir}") == "a {dir}", "a folder that spells the token is not expanded again");
        Check(ExpandDir("notepad", "C:\\x") == "notepad", "no token");
    }

    void _flatten() {
        Check(Flatten("  a\r\nb\tc  ", 120) == "a  b c", "line breaks and tabs become spaces, ends are trimmed");
        Check(Flatten(" \r\n ", 120).empty(), "only blanks");
        Check(Flatten("abcdef", 6) == "abcdef", "exactly the limit is kept");
        Check(Flatten("abcdefg", 6) == "abcdef...", "over the limit is cut");

        const std::string cyrillic = "\xD0\x96\xD0\x96\xD0\x96";
        Check(Flatten(cyrillic, 6) == cyrillic, "whole code points at the limit");
        Check(Flatten(cyrillic + "a", 5) == "\xD0\x96\xD0\x96...", "a cut inside a code point backs up");
        Check(Flatten("\xE4\xB8\xAD\xE4\xB8\xAD", 4) == "\xE4\xB8\xAD...", "three-byte cut backs up");
        Check(Flatten("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 6) == "\xF0\x9F\x98\x80...", "four-byte cut backs up");
    }

    void _wholeCodePoints() {
        Check(WholeCodePoints("abc") == "abc", "ascii");
        Check(WholeCodePoints("").empty(), "empty");
        Check(WholeCodePoints("a\xD0") == "a", "lead byte alone");
        Check(WholeCodePoints("a\xD0\x96") == "a\xD0\x96", "complete two-byte");
        Check(WholeCodePoints("a\xE4\xB8") == "a", "three-byte cut after two");
        Check(WholeCodePoints("a\xF0\x9F\x98") == "a", "four-byte cut after three");
        Check(WholeCodePoints("a\xF0\x9F\x98\x80") == "a\xF0\x9F\x98\x80", "complete four-byte");
        Check(WholeCodePoints("\x80\x80\x80\x80") == "\x80\x80\x80\x80", "stray continuation bytes are left for the decoder");
    }
}

int main() {
    _split();
    _quote();
    _expandDir();
    _flatten();
    _wholeCodePoints();

    std::printf(Test::Failures ? "%d check(s) failed\n" : "all command line checks passed\n", Test::Failures);
    return Test::Failures ? 1 : 0;
}
