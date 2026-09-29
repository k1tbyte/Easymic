#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct Fnv1a {
    size_t operator()(uint64_t v) const {
        size_t h = 14695981039346656037ull;
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ull; }
        return h;
    }
};
struct Binding { std::function<void()> a, b, c; bool block = false; };
struct Scope { std::vector<Binding> bindings; bool block = false; };
struct MaskEntry { Scope global; std::unordered_map<std::string, Scope> apps; };

int main() {
    std::unordered_map<uint64_t, MaskEntry, Fnv1a> table;
    std::bitset<256> endsMask;
    const uint8_t bound[] = {'M', 'K', 'L', 'B', 'T', 0x70, 0x71, 0x79};
    for (uint8_t vk : bound) {
        for (uint64_t mods : {0x04ull, 0x0Cull, 0x14ull, 0ull}) {
            table[(uint64_t{vk} << 8) | mods].global.bindings.resize(1);
            endsMask[vk] = true;
        }
    }
    constexpr int N = 50'000'000;
    uint8_t stream[64];
    for (int i = 0; i < 64; ++i) stream[i] = "etaoinshrdlucmfwypvbgkjqxz "[i % 27];

    auto run = [&](const char* name, auto&& body) {
        volatile size_t sink = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) sink = sink + body(stream[i & 63]);
        auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-28s %6.2f ns/key\n", name, ns / N);
    };

    run("two hash lookups (today)", [&](uint8_t vk) {
        const uint64_t single = uint64_t{vk} << 8, chord = single | 0x04;
        auto a = table.find(single);
        auto b = table.find(chord);
        return (a != table.end()) + (b != table.end());
    });
    run("bitset guard then lookups", [&](uint8_t vk) {
        if (!endsMask[vk]) return size_t{0};
        const uint64_t single = uint64_t{vk} << 8, chord = single | 0x04;
        auto a = table.find(single);
        auto b = table.find(chord);
        return size_t((a != table.end()) + (b != table.end()));
    });
    run("guard only (floor)", [&](uint8_t vk) { return size_t(endsMask[vk]); });
}
