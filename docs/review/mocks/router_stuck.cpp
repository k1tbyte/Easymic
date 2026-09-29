#include <cstdio>
#include "Input/Router.hpp"
using namespace Input;

static Verdict answer = Verdict::Next;
static Verdict OnKey(const KeyEvent&) { return answer; }

int main() {
    Router::Add({.Id = "hot", .Order = 200, .OnKey = &OnKey});
    constexpr uint8_t K = 'A';

    std::puts("S1: stage enabled while K is held, K autorepeats, stage consumes");
    answer = Verdict::Next;
    bool d = !Router::Key(K, true, false, 0);
    std::printf("  down delivered to app: %d\n", d);
    answer = Verdict::Consume;
    Router::Enable(0, WantKeys);
    bool rep = Router::Key(K, true, false, 0);
    bool up = Router::Key(K, false, false, 0);
    std::printf("  autorepeat swallowed: %d, up swallowed: %d -> app saw down without up: %d\n", rep, up, d && up);

    std::puts("S2: same after Router::Reset (hook re-installed while K held)");
    Router::Disable(0);
    Router::Enable(0, WantKeys);
    answer = Verdict::Next;
    d = !Router::Key(K, true, false, 0);
    Router::Reset();
    answer = Verdict::Consume;
    rep = Router::Key(K, true, false, 0);
    up = Router::Key(K, false, false, 0);
    std::printf("  autorepeat swallowed: %d, up swallowed: %d -> app saw down without up: %d\n", rep, up, d && up);
}
