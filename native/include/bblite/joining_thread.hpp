#pragma once

#include <bblite/teardown.hpp>
#include <atomic>
#include <concepts>
#include <memory>
#include <thread>
#include <utility>

namespace bbl {

/** Cooperative cancellation for native work on platforms without C++20 stop tokens. */
class StopToken {
    std::shared_ptr<std::atomic<bool>> requested_;
    friend class JoiningThread;
    explicit StopToken(std::shared_ptr<std::atomic<bool>> requested)
        : requested_(std::move(requested)) {}

public:
    bool stop_requested() const {
        return requested_ && requested_->load(std::memory_order_acquire);
    }
};

/** Requests cancellation and joins on scope exit, including on older libc++. */
class JoiningThread {
    std::shared_ptr<std::atomic<bool>> stop_;
    std::thread thread_;

    void finish() {
        request_stop();
        if (thread_.joinable())
            run_teardown("native thread join", [&] { thread_.join(); });
    }

public:
    JoiningThread() = default;
    template <typename Function>
        requires(std::invocable<Function> || std::invocable<Function, StopToken>)
    explicit JoiningThread(Function&& function) {
        if constexpr (std::invocable<Function, StopToken>) {
            stop_ = std::make_shared<std::atomic<bool>>(false);
            thread_ = std::thread(std::forward<Function>(function), get_stop_token());
        } else {
            thread_ = std::thread(std::forward<Function>(function));
        }
    }
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;
    JoiningThread(JoiningThread&&) noexcept = default;
    JoiningThread& operator=(JoiningThread&& other) noexcept {
        if (this != &other) {
            finish();
            stop_ = std::move(other.stop_);
            thread_ = std::move(other.thread_);
        }
        return *this;
    }
    ~JoiningThread() { finish(); }
    void request_stop() {
        if (stop_)
            stop_->store(true, std::memory_order_release);
    }
    StopToken get_stop_token() const { return StopToken(stop_); }
    std::thread::id get_id() const { return thread_.get_id(); }
};

} // namespace bbl
