// The router's rules, driven with made-up events. Run with .\build.ps1 -Test
#include <cstdio>
#include <string>
#include <vector>

#include "Input/Router.hpp"

using namespace Input;

namespace {

    int _failures = 0;

    void Check(const bool condition, const char* what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++_failures;
        }
    }

    struct Probe {
        char Name;
        Verdict Answer = Verdict::Next;
        int Resets = 0;
        void (*During)(const KeyEvent&) = nullptr;
    };

    Probe _a{'a'}, _b{'b'}, _c{'c'};
    std::string _trace;

    template <Probe& P>
    Verdict _onKey(const KeyEvent& event) {
        _trace += _trace.empty() ? "" : " ";
        _trace += P.Name;
        _trace += event.Down ? (event.Repeat ? '*' : '+') : '-';
        _trace += static_cast<char>(event.Vk);
        if (P.During) {
            P.During(event);
        }
        return P.Answer;
    }

    template <Probe& P>
    void _onReset() { ++P.Resets; }

    constexpr uint8_t A = 'A', B = 'B', Button = VK_LBUTTON;
    size_t a, b, c;

    bool Press(const uint8_t vk, const uint8_t level = 0) { return Router::Key(vk, true, false, level); }
    bool Release(const uint8_t vk, const uint8_t level = 0) { return Router::Key(vk, false, false, level); }

    void Fresh() {
        for (const size_t stage : {a, b, c}) {
            Router::Enable(stage, WantKeys);
        }
        Router::Reset();
        for (Probe* probe : {&_a, &_b, &_c}) {
            probe->Answer = Verdict::Next;
            probe->During = nullptr;
        }
        _trace.clear();
    }

    void PassesThroughEveryStage() {
        Fresh();
        Check(!Press(A) && !Release(A), "an unanswered event is delivered");
        Check(_trace == "a+A b+A c+A a-A b-A c-A", "every stage sees the down and the up, in order");
    }

    void RepeatIsFlagged() {
        Fresh();
        Press(A);
        Press(A);
        Release(A);
        Check(_trace == "a+A b+A c+A a*A b*A c*A a-A b-A c-A", "a second down is a repeat");
    }

    void ConsumeOwnsRepeatsAndUp() {
        Fresh();
        _b.Answer = Verdict::Consume;
        Check(Press(A), "a consumed down is swallowed");
        _b.Answer = Verdict::Next;
        Check(Press(A) && Release(A), "its repeat and up are swallowed whatever the owner answers");
        Check(_trace == "a+A b+A a*A b*A a-A b-A", "the repeat and the up stop at the owner");
        _trace.clear();
        Check(!Press(A), "ownership ends with the up");
        Check(_trace == "a+A b+A c+A", "and the next press is nobody's repeat");
        Release(A);
    }

    void DeliverStopsThePipeline() {
        Fresh();
        _a.Answer = Verdict::Deliver;
        Check(!Press(A), "a delivered down goes to the app");
        _a.Answer = Verdict::Next;
        Release(A);
        Check(_trace == "a+A a-A", "later stages see neither the down nor its up");
    }

    void NoUpWithoutItsDown() {
        Fresh();
        Router::Disable(b);
        Press(A);
        const int resets = _b.Resets;
        Router::Enable(b, WantKeys);
        Check(_b.Resets == resets + 1, "enabling starts the stage over");
        Release(A);
        Check(_trace == "a+A c+A a-A c-A", "a stage enabled mid-press never gets the up");
    }

    void ConsumingAnUpIsIgnored() {
        Fresh();
        Press(A);
        _b.Answer = Verdict::Consume;
        Check(!Release(A), "an up nobody owns is never swallowed");
        Check(_trace == "a+A b+A c+A a-A b-A c-A", "and the stages after still see it");
    }

    void WantsFilterTheKind() {
        Fresh();
        Router::Enable(b, WantButtons);
        Router::Key(Button, true, true, 0);
        Router::Key(Button, false, true, 0);
        Press(A);
        Release(A);
        Check(_trace == "b+\x01 b-\x01 a+A c+A a-A c-A", "a stage sees only what it asked for");
    }

    void LevelsSkipTheSender() {
        Fresh();
        Check(!Press(A, 2) && !Release(A, 2), "sent input is delivered");
        Check(_trace == "c+A c-A", "input sent by b is seen only after b");
        _trace.clear();
        Check(!Press(A, 3) && !Press(A, 200), "input sent past every stage goes straight through");
        Check(_trace.empty(), "and no stage sees it");
    }

    void OwnershipIsPerLevel() {
        Fresh();
        _a.Answer = Verdict::Consume;
        Press(A);
        _a.Answer = Verdict::Next;
        Check(!Press(A, 1), "the key a sends back is not a repeat of the one it ate");
        Release(A, 1);
        Check(Release(A), "the eaten key's up is still swallowed");
        Check(_trace == "a+A b+A c+A b-A c-A a-A", "each level keeps its own owner");
    }

    void DisabledOwnerStillSwallows() {
        Fresh();
        _b.Answer = Verdict::Consume;
        Press(A);
        Router::Disable(b);
        Check(Release(A), "the app never saw the down, so the up stays eaten");
        Check(_trace == "a+A b+A a-A", "a disabled owner is not called, the stage before it still is");
    }

    void ResetForgetsEverything() {
        Fresh();
        _b.Answer = Verdict::Consume;
        Press(A);
        const int resets = _a.Resets;
        Router::Disable(c);
        Router::Reset();
        Check(_a.Resets == resets + 1, "a reset tells every enabled stage");
        Check(!Release(A), "nothing is owned after a reset");
        Check(_trace == "a+A b+A", "and nobody saw a down since, so nobody gets the up");
    }

    void SendReentersInsideTheSender() {
        Fresh();
        _a.Answer = Verdict::Consume;
        _a.During = [](const KeyEvent& event) {
            if (event.Vk == A && event.Down && !event.Repeat) {
                Router::Key(B, true, false, 1);
                Router::Key(B, false, false, 1);
            }
        };
        Check(Press(A), "the sender's own verdict still holds");
        Check(_trace == "a+A b+B c+B b-B c-B", "what a sends walks the later stages before a returns");
        _a.During = nullptr;
        Release(A);
    }

    void NeededIsTheUnion() {
        Fresh();
        Router::Disable(a);
        Router::Enable(b, WantButtons);
        Check(Router::Needed() == (WantKeys | WantButtons), "keys from c, buttons from b");
        Router::Disable(b);
        Router::Disable(c);
        Check(Router::Needed() == 0, "nothing enabled wants nothing");
    }

} // anonymous namespace

int main() {
    Router::Add({.Id = "c", .Order = 200, .OnKey = &_onKey<_c>, .OnReset = &_onReset<_c>});
    Router::Add({.Id = "a", .Order = 0, .OnKey = &_onKey<_a>, .OnReset = &_onReset<_a>});
    Router::Add({.Id = "b", .Order = 100, .OnKey = &_onKey<_b>, .OnReset = &_onReset<_b>});
    a = Router::Find("a");
    b = Router::Find("b");
    c = Router::Find("c");
    Check(a == 0 && b == 1 && c == 2, "stages sort by Order, not by the order they were added");

    PassesThroughEveryStage();
    RepeatIsFlagged();
    ConsumeOwnsRepeatsAndUp();
    DeliverStopsThePipeline();
    NoUpWithoutItsDown();
    ConsumingAnUpIsIgnored();
    WantsFilterTheKind();
    LevelsSkipTheSender();
    OwnershipIsPerLevel();
    DisabledOwnerStillSwallows();
    ResetForgetsEverything();
    SendReentersInsideTheSender();
    NeededIsTheUnion();

    std::printf(_failures ? "%d check(s) failed\n" : "all router checks passed\n", _failures);
    return _failures ? 1 : 0;
}
