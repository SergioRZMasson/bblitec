#pragma once

#include <bblite/joining_thread.hpp>
#include <bblite/teardown.hpp>
#include <bblite/uncaught_error.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace bbl::pal {

namespace detail {

/** One unit of native work: a worker runs it unless its owner withdrew it first. */
class NativeWorkItem {
public:
    NativeWorkItem() = default;
    NativeWorkItem(const NativeWorkItem&) = delete;
    NativeWorkItem& operator=(const NativeWorkItem&) = delete;
    virtual ~NativeWorkItem() = default;

    /** On a worker: run the work unless its owner withdrew it. The work's own
     * failure is its result; any other escapes the worker and ends the process. */
    void run() {
        auto queued = Phase::queued;
        if (phase_.compare_exchange_strong(queued, Phase::started, std::memory_order_acq_rel))
            execute();
    }
    /** On the owner: true, with the work released, when no worker had started it. */
    bool withdraw() noexcept {
        auto queued = Phase::queued;
        if (!phase_.compare_exchange_strong(queued, Phase::withdrawn, std::memory_order_acq_rel))
            return false;
        release();
        return true;
    }
    /** On the owner, once the result is ready: release the work and its captures. */
    virtual void release() noexcept = 0;

private:
    enum class Phase : unsigned char { queued, started, withdrawn };
    std::atomic<Phase> phase_{Phase::queued};
    virtual void execute() = 0;
};

template <typename Output, typename Work> class NativeWorkClosure final : public NativeWorkItem {
public:
    NativeWorkClosure(Work work, std::function<void()> finished)
        : work_(std::move(work)), finished_(std::move(finished)) {}
    std::future<Output> result() { return result_.get_future(); }
    void release() noexcept override { work_.reset(); }

private:
    std::optional<Work> work_;
    std::function<void()> finished_;
    std::promise<Output> result_;

    void execute() override {
        // `run` starts the work only while it is queued, and `release` comes
        // after the result or a withdrawal, so the work is still held here.
        try {
            if constexpr (std::is_void_v<Output>) {
                (*work_)();
                result_.set_value();
            } else {
                result_.set_value((*work_)());
            }
        } catch (...) {
            result_.set_exception(std::current_exception());
        }
        if (finished_)
            finished_();
    }
};

/** Whether this thread is one of the native workers (`NativeWorkers`). */
inline thread_local bool on_native_worker = false;

} // namespace detail

/** The physics solver's default thread count (`pal_physics_scheduler.hpp`). */
inline constexpr unsigned default_physics_threads = 8;

/**
 * The process's native worker threads. A thread starts when submitted work
 * finds none idle, up to the hardware threads less the physics solver's
 * default share (at most half of them), so a load overlapping physics does not
 * oversubscribe the machine, and serves work in submission order; a thread
 * idle for a second exits.
 * Native work never waits on other native work: once every thread waited,
 * none would be left to run it (`NativeWork` refuses to). Joined at process
 * exit, when no owner (`NativeWork`) can remain.
 */
class NativeWorkers {
public:
    static NativeWorkers& instance() {
        static NativeWorkers workers;
        return workers;
    }
    NativeWorkers(const NativeWorkers&) = delete;
    NativeWorkers& operator=(const NativeWorkers&) = delete;
    ~NativeWorkers() {
        std::vector<JoiningThread> threads;
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            threads.swap(threads_);
        }
        wake_.notify_all();
    }

    /** The most threads it runs at once. */
    std::size_t capacity() const noexcept { return limit_; }

    void submit(std::shared_ptr<detail::NativeWorkItem> item) {
        std::vector<JoiningThread> exited;
        {
            std::lock_guard lock(mutex_);
            if (stopping_)
                throw std::logic_error("Native workers are shutting down.");
            exited = take_exited();
            // Every idle thread already has queued work waiting for it.
            if (queue_.size() >= idle_ && threads_.size() < limit_)
                threads_.emplace_back([this] { serve(); });
            queue_.push_back(std::move(item));
        }
        wake_.notify_one();
    }

private:
    static constexpr std::chrono::seconds idle_exit{1};

    NativeWorkers() {
        const unsigned hardware = std::thread::hardware_concurrency();
        limit_ = std::max(2u, hardware - std::min(default_physics_threads, hardware / 2));
    }

    void serve() {
        detail::on_native_worker = true;
        std::unique_lock lock(mutex_);
        for (;;) {
            ++idle_;
            const bool woken =
                wake_.wait_for(lock, idle_exit, [&] { return stopping_ || !queue_.empty(); });
            --idle_;
            if (stopping_)
                return;
            if (!woken) {
                // Joined by the next submission, or at exit.
                exited_.push_back(std::this_thread::get_id());
                return;
            }
            auto item = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            item->run();
            item.reset();
            lock.lock();
        }
    }

    /** Under `mutex_`: the threads that exited idle, to join outside it. */
    std::vector<JoiningThread> take_exited() {
        std::vector<JoiningThread> exited;
        for (const auto id : exited_) {
            const auto found =
                std::find_if(threads_.begin(), threads_.end(),
                             [&](const JoiningThread& thread) { return thread.get_id() == id; });
            exited.push_back(std::move(*found));
            threads_.erase(found);
        }
        exited_.clear();
        return exited;
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<detail::NativeWorkItem>> queue_;
    std::vector<JoiningThread> threads_;
    std::vector<std::thread::id> exited_;
    std::size_t idle_ = 0;
    bool stopping_ = false;
    std::size_t limit_ = 2;
};

template <typename Output> class NativeWork;

/**
 * Run `work` on the native workers. `work` takes and returns native data
 * only; `finished`, when given, runs on the worker once the result is ready.
 */
template <typename Work>
NativeWork<std::invoke_result_t<Work&>> start_native_work(Work work,
                                                          std::function<void()> finished = {});

/**
 * Native work's result, owned by the thread that started it. That thread, not
 * a worker, releases the work's captures and takes its result. Destroying the
 * owner withdraws work no worker has started, or waits for running work, so
 * no work outlives its owner. On a native worker, `get` throws `logic_error`
 * and a destruction that would wait ends the process (`run_teardown`).
 */
template <typename Output> class NativeWork {
public:
    NativeWork() = default;
    NativeWork(NativeWork&&) noexcept = default;
    NativeWork& operator=(NativeWork&& other) noexcept {
        if (this != &other) {
            finish();
            item_ = std::move(other.item_);
            result_ = std::move(other.result_);
        }
        return *this;
    }
    NativeWork(const NativeWork&) = delete;
    NativeWork& operator=(const NativeWork&) = delete;
    ~NativeWork() { finish(); }

    /** Whether the work has finished; `get` then returns without waiting. */
    bool ready() const {
        return result_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    /** The work's result, or its exception rethrown, once it finishes; never
     * on a native worker. */
    Output get() {
        refuse_on_worker();
        result_.wait();
        release();
        return result_.get();
    }

private:
    template <typename Work>
    friend NativeWork<std::invoke_result_t<Work&>> start_native_work(Work, std::function<void()>);
    NativeWork(std::shared_ptr<detail::NativeWorkItem> item, std::future<Output> result)
        : item_(std::move(item)), result_(std::move(result)) {}

    std::shared_ptr<detail::NativeWorkItem> item_;
    std::future<Output> result_;

    static void refuse_on_worker() {
        if (detail::on_native_worker)
            throw std::logic_error("Native work cannot wait on other native work.");
    }
    void release() noexcept {
        if (const auto item = std::exchange(item_, nullptr))
            item->release();
    }
    void finish() noexcept {
        if (!item_)
            return;
        if (item_->withdraw()) {
            item_.reset();
            return;
        }
        run_teardown("NativeWork owner", [] { refuse_on_worker(); });
        try {
            static_cast<void>(get());
        } catch (...) {
            // Discarded work's failure has no observer.
            discard_exception();
        }
    }
};

template <typename Work>
NativeWork<std::invoke_result_t<Work&>> start_native_work(Work work,
                                                          std::function<void()> finished) {
    using Output = std::invoke_result_t<Work&>;
    auto item = std::make_shared<detail::NativeWorkClosure<Output, Work>>(std::move(work),
                                                                          std::move(finished));
    NativeWork<Output> owner(item, item->result());
    NativeWorkers::instance().submit(std::move(item));
    return owner;
}

} // namespace bbl::pal
