#include <cstdio>
#include <random>
#include <deque>
#include <vector>
#include "Input/Hold.hpp"
using namespace Input;

int main() {
    std::mt19937 rng(7);
    long runs = 0, lost = 0, dup = 0, order = 0, stuckHold = 0, editAfter = 0;
    for (int run = 0; run < 20000; ++run, ++runs) {
        uint64_t now = 1000 + run * 100000ull;
        std::vector<int> taken, sent;
        std::deque<int> echo;
        std::vector<INPUT> out;
        int nextId = 1, editIdBase = 100000;
        HoldId id = Hold::Begin(now);
        bool committed = false;
        auto flush = [&] {
            while (Hold::Outgoing(out)) {
                size_t drop = 0;
                for (size_t i = 0; i < out.size() - drop; ++i) { sent.push_back(out[i].ki.time); echo.push_back(out[i].ki.time); }
                Hold::Unsent(drop);
            }
        };
        int steps = 20 + rng() % 200;
        for (int s = 0; s < steps; ++s) {
            now += rng() % 30;
            switch (rng() % 6) {
                case 0: case 1: {
                    if (!Hold::Active()) break;
                    INPUT e{}; e.ki.time = nextId++; taken.push_back(e.ki.time);
                    Hold::Take(e, now); flush(); break;
                }
                case 2: {
                    if (committed || !id) break;
                    std::vector<INPUT> edit(1 + rng() % 8);
                    for (auto& e : edit) e.ki.time = editIdBase++;
                    if (Hold::Commit(id, edit, now)) { committed = true; }
                    flush(); break;
                }
                case 3: case 4:
                    if (!echo.empty()) { echo.pop_front(); Hold::Arrived(); flush(); }
                    break;
                case 5: now += rng() % 200; Hold::Tick(now); flush(); break;
            }
        }
        for (int k = 0; k < 400 && Hold::Active(); ++k) {
            now += 20;
            if (!echo.empty()) { echo.pop_front(); Hold::Arrived(); } else Hold::Tick(now);
            flush();
        }
        if (Hold::Active()) { ++stuckHold; continue; }
        std::vector<int> phys;
        for (int v : sent) if (v < 100000) phys.push_back(v);
        if (phys.size() < taken.size()) ++lost;
        if (phys.size() > taken.size()) ++dup;
        if (phys != taken) ++order;
        Hold::Tick(UINT64_MAX); Hold::Tick(UINT64_MAX); flush();
        if (committed) {  bool seenPhys = false; for (int v : sent) { if (v < 100000) seenPhys = true; else if (seenPhys) { ++editAfter; break; } } }
    }
    std::printf("runs=%ld lost=%ld dup=%ld reordered=%ld holdNeverEnded=%ld editAfterHeld=%ld\n", runs, lost, dup, order, stuckHold, editAfter);
}
