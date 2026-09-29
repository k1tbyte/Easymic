#include "Judge.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <utility>

#include "Autocorrect.hpp"
#include "Core/Input/Input.hpp"
#include "Features/Keyboard/Layout/InputLanguage.hpp"
#include "Features/Keyboard/Autocorrect/Rules/Learning.hpp"

namespace Judge {

namespace {

    struct Snapshot {
        std::array<Convert::Key, TypedWord::MaxKeys> Keys{};
        uint8_t Count = 0;
        bool EarlyTried = false;
        uint32_t Gen = 0;
    };

    struct Result {
        uint32_t Gen = 0;
        HKL Layout = nullptr;
        Convert::Verdict Verdict;
        Convert::Verdict Early;
    };

    EarlyFix _onEarly = nullptr;

    // Written by the input thread, read by the runs
    SRWLOCK _lock = SRWLOCK_INIT;
    Snapshot _slot;
    std::shared_ptr<const Autocorrect::Runtime> _runtime;
    /// Coalesces a burst of keys into one run on the latest word.
    std::atomic<bool> _pending{false};
    PTP_WORK _work = nullptr;
    Convert::Verdict _kept;
    PTP_WORK _logWork = nullptr;

    // Input thread only
    Result _ready;

    void CALLBACK _log(PTP_CALLBACK_INSTANCE, void*, PTP_WORK) {
        AcquireSRWLockExclusive(&_lock);
        const Convert::Verdict verdict = std::exchange(_kept, {});
        const auto runtime = _runtime;
        ReleaseSRWLockExclusive(&_lock);
        if (runtime) {
            Autocorrect::Log(*runtime, verdict);
        }
    }

    void CALLBACK _run(PTP_CALLBACK_INSTANCE, void*, PTP_WORK) {
        _pending = false;
        AcquireSRWLockShared(&_lock);
        const Snapshot word = _slot;
        const auto runtime = _runtime;
        ReleaseSRWLockShared(&_lock);
        if (!runtime || !runtime->Auto || !word.Count) {
            return;
        }
        const HKL layout = InputLanguage::LayoutOf(InputLanguage::FocusedWindow());
        if (!runtime->Reads(layout)) {
            return;
        }
        const int from = runtime->SideOf(layout);
        const auto learned = Learning::Current();
        const std::span<const Convert::Key> keys{word.Keys.data(), word.Count};
        Result result{word.Gen, layout, Autocorrect::Decide(*runtime, *learned, keys, from)};
        if (runtime->MidWord && !word.EarlyTried) {
            result.Early = Autocorrect::Early(*runtime, *learned, keys, from);
        }
        Input::Post([result = std::move(result)]() mutable {
            // Runs may finish out of order: an older word never replaces a newer one
            if (static_cast<int32_t>(result.Gen - _ready.Gen) > 0) {
                _ready = std::move(result);
                if (_ready.Early.WrongLayout) {
                    _onEarly(_ready.Gen, _ready.Layout, _ready.Early);
                }
            }
        });
    }

} // anonymous namespace

    void Register(const EarlyFix early) {
        _onEarly = early;
        _work = CreateThreadpoolWork(&_run, nullptr, nullptr);
        _logWork = CreateThreadpoolWork(&_log, nullptr, nullptr);
        // Registered after the statics a run reads were built, so it runs before they are destroyed
        std::atexit([] {
            for (PTP_WORK* work : {&_work, &_logWork}) {
                if (*work) {
                    WaitForThreadpoolWorkCallbacks(*work, TRUE);
                    CloseThreadpoolWork(std::exchange(*work, nullptr));
                }
            }
        });
    }

    void Use(std::shared_ptr<const Autocorrect::Runtime> runtime) {
        AcquireSRWLockExclusive(&_lock);
        std::swap(_runtime, runtime);
        _kept = {};
        ReleaseSRWLockExclusive(&_lock);
        _ready = {};
    }

    void Typed(const TypedWord::Word& word, const uint32_t gen) {
        if (!_work) {
            return;
        }
        AcquireSRWLockExclusive(&_lock);
        std::copy_n(word.Keys.begin(), word.Count, _slot.Keys.begin());
        _slot.Count = word.Count;
        _slot.EarlyTried = word.EarlyAt != 0;
        _slot.Gen = gen;
        ReleaseSRWLockExclusive(&_lock);
        if (!_pending.exchange(true)) {
            SubmitThreadpoolWork(_work);
        }
    }

    const Convert::Verdict* Ready(const uint32_t gen, const HKL layout) {
        return _ready.Gen == gen && _ready.Layout == layout ? &_ready.Verdict : nullptr;
    }

    void LogKept() {
        if (!_logWork || !_runtime || !_runtime->LogDecisions) {
            return;
        }
        // A swap allocates nothing on the input thread
        AcquireSRWLockExclusive(&_lock);
        std::swap(_kept, _ready.Verdict);
        ReleaseSRWLockExclusive(&_lock);
        _ready.Layout = nullptr;
        SubmitThreadpoolWork(_logWork);
    }
}
