#pragma once
#include <coroutine>
#include <exception>
#include <utility>

namespace ls::internal {

// Minimal coroutine generator (C++20 stand-in for std::generator, C++23).
// The execution core yields step events through one of these; generate(),
// generation::step(), and the debug tooling are all just pullers.
template <class T>
class sequence {
public:
    struct promise_type {
        T current{};

        sequence get_return_object() {
            return sequence{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept   { return {}; }
        std::suspend_always yield_value(T v) noexcept  { current = std::move(v); return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    sequence() = default;
    explicit sequence(std::coroutine_handle<promise_type> h) : h_(h) {}
    sequence(sequence&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    sequence& operator=(sequence&& o) noexcept {
        if (h_) h_.destroy();
        h_ = std::exchange(o.h_, {});
        return *this;
    }
    sequence(sequence const&)            = delete;
    sequence& operator=(sequence const&) = delete;
    ~sequence() { if (h_) h_.destroy(); }

    // Advance to the next yield; false once the coroutine has finished.
    bool next() {
        if (!h_ || h_.done()) return false;
        h_.resume();
        return !h_.done();
    }
    T const& value() const { return h_.promise().current; }

private:
    std::coroutine_handle<promise_type> h_{};
};

}  // namespace ls::internal
