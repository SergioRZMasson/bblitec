#pragma once

#include <bblite/js_promise.hpp>
#include <bblite/pal_event_loop.hpp>
#include <bblite/pal_native_workers.hpp>

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace bbl::pal {

/**
 * Run `work` on the native workers, then settle the returned promise with
 * `settle(output)` on the current realm; a throw from either rejects it.
 * `work` takes and returns native data only: no JS values, realm records or
 * callbacks cross to the worker.
 *
 * Jobs a realm starts settle in start order, each in its own task, while the
 * realm keeps servicing its other tasks and timers
 * (`EventLoop::register_ordered_completion`). A realm that closes first
 * withdraws a job no worker has started, or waits for a running one, and
 * discards its settlement.
 */
template <typename T, typename Work, typename Settle>
js::Promise<T> run_native_job(Work work, Settle settle) {
    using Output = std::invoke_result_t<Work&>;
    static_assert(!std::is_void_v<Output>, "A native job returns the data it settles.");
    js::Promise<T> promise;
    auto& loop = EventLoop::current();
    // The realm-owned handler owns the job, so the realm releases its captures and output.
    auto job = std::make_shared<NativeWork<Output>>();
    const auto id = loop.register_ordered_completion(
        [promise, job, settle = std::move(settle)](std::unique_ptr<ExternalEvent>) mutable {
            try {
                promise.resolve(settle(job->get()));
            } catch (const WorkerTerminated&) {
                throw;
            } catch (...) {
                promise.reject(std::current_exception());
            }
        });
    try {
        *job = start_native_work(std::move(work), [inbox = loop.inbox(), id] {
            inbox->post(std::make_unique<CompletionEvent>(id));
        });
    } catch (...) {
        loop.cancel_completion(id);
        throw;
    }
    return promise;
}

} // namespace bbl::pal
