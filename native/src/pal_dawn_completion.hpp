#pragma once
#include <bblite/pal_offscreen.hpp>
#include <bblite/joining_thread.hpp>
#include <webgpu/webgpu.h>
#include <atomic>
#include <limits>
#include <type_traits>

namespace bbl::pal {
struct DawnCompletionState {
    std::function<void(std::exception_ptr)> complete;
    std::atomic<bool> delivered = false;
    explicit DawnCompletionState(std::function<void(std::exception_ptr)> callback)
        : complete(std::move(callback)) {}
    void deliver(std::exception_ptr error) {
        if (!delivered.exchange(true))
            complete(error);
    }
};

/** Drive a native future even while the realm is awaiting its first rendered frame. */
struct DawnFutureCompletion final : OffscreenCompletion {
    JoiningThread waiter;
    static auto instance_owner(WGPUInstance instance) {
        wgpuInstanceAddRef(instance);
        return std::shared_ptr<std::remove_pointer_t<WGPUInstance>>(instance, wgpuInstanceRelease);
    }
    DawnFutureCompletion(WGPUInstance instance, WGPUFuture future,
                         std::shared_ptr<DawnCompletionState> state)
        : waiter([owner = instance_owner(instance), future, state = std::move(state)] {
              WGPUFutureWaitInfo info{};
              info.future = future;
              const auto status = wgpuInstanceWaitAny(owner.get(), 1, &info,
                                                      std::numeric_limits<std::uint64_t>::max());
              if (status != WGPUWaitStatus_Success)
                  state->deliver(std::make_exception_ptr(
                      std::runtime_error("Dawn asynchronous GPU completion wait failed.")));
          }) {}
};
} // namespace bbl::pal
