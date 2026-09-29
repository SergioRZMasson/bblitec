#include <bblite/js_realm_state.hpp>
#include <bblite/pal_compute_command.hpp>
#include <cassert>
#include <iostream>

namespace {
struct QuerySet final : bbl::pal::GpuTimestampQuerySet {};
struct Readback final : bbl::pal::GpuTimestampReadback {
    bool ready = false;
    bool fail = false;
    std::vector<std::uint64_t> values;
    std::optional<std::vector<std::uint64_t>> poll() override {
        if (fail)
            throw std::runtime_error("");
        if (!ready)
            return {};
        return values;
    }
};
struct Device final : bbl::pal::OffscreenDevice {
    unsigned created = 0;
    std::vector<std::shared_ptr<Readback>> submitted;
    bool supports_gpu_timestamps() const override { return true; }
    std::shared_ptr<bbl::pal::GpuTimestampQuerySet>
    create_gpu_timestamp_query_set(std::uint32_t count) override {
        assert(count == 128);
        ++created;
        return std::make_shared<QuerySet>();
    }
    std::shared_ptr<bbl::pal::GpuTimestampReadback>
    resolve_gpu_timestamps(const std::shared_ptr<bbl::pal::GpuTimestampQuerySet>&,
                           std::uint32_t count) override {
        auto result = std::make_shared<Readback>();
        result->values.resize(count);
        submitted.push_back(result);
        return result;
    }
};

bbl::js::Promise<bbl::js::PromiseVoid> check(bbl::pal::EventLoop& loop) {
    using namespace bbl;
    auto unsupported = std::make_shared<pal::GpuTaskTimingState>();
    assert(get_render_task_gpu_timings(unsupported)->status == "unsupported");
    assert((co_await set_render_task_gpu_timing_enabled(unsupported, true))->status ==
           "unsupported");
    auto device = std::make_shared<Device>();
    auto state = std::make_shared<pal::GpuTaskTimingState>();
    state->device = device;
    state->supported = true;
    assert(get_render_task_gpu_timings(state)->status == "disabled");
    auto enabling = set_render_task_gpu_timing_enabled(state, true);
    assert(get_render_task_gpu_timings(state)->status == "pending");
    auto disabling = set_render_task_gpu_timing_enabled(state, false);
    assert((co_await enabling)->status == "disabled");
    assert((co_await disabling)->status == "disabled");
    assert(device->created == 0);
    assert((co_await set_render_task_gpu_timing_enabled(state, true))->status == "pending");
    assert(device->created == 1);
    auto timer = state->timer;
    const auto timed_task = [&](const std::string& name) {
        timer->begin_task();
        const auto writes = timer->pass_timestamps(false);
        timer->end_task(name);
        return writes;
    };
    // Compute tasks record through the encoder, which times each pass they
    // open, one with no dispatch included.
    pal::ComputeCommandEncoder encoder(device);
    encoder.timed_pass = [&] { return timer->pass_timestamps(false); };
    timer->begin_frame();
    timer->begin_task();
    auto dispatches = encoder.begin_compute_pass("dispatches");
    dispatches.dispatch(1, 1, 1);
    dispatches.dispatch(2, 1, 1);
    dispatches.end();
    timer->end_task("renamed compute");
    timer->begin_task();
    encoder.begin_compute_pass("empty").end();
    auto later = encoder.begin_compute_pass("later");
    later.dispatch(1, 1, 1);
    later.end();
    timer->end_task("mipmaps");
    const auto written = [&](std::size_t at, std::uint32_t index, bool beginning) {
        const auto* write = std::get_if<pal::GpuTimestampWrite>(&encoder.commands.at(at));
        return write && write->index == index && write->beginning == beginning;
    };
    const auto dispatched = [&](std::size_t at) {
        return std::holds_alternative<pal::ComputeDispatch>(encoder.commands.at(at));
    };
    assert(encoder.commands.size() == 8 && written(0, 0, true) && dispatched(1) && dispatched(2) &&
           written(3, 1, false) && written(4, 2, true) && written(5, 3, false) && dispatched(6) &&
           written(7, 4, false));
    // A task that opens no pass keeps its index but is not measured.
    timer->begin_task();
    timer->end_task("empty");
    timer->begin_task();
    const auto backwards = timer->pass_timestamps(false);
    timer->end_task("backwards");
    // A pass carrying timestamps of its own leaves its task unmeasured.
    timer->begin_task();
    assert(!timer->pass_timestamps(true) && !timer->pass_timestamps(false));
    timer->end_task("conflicted");
    assert(backwards && backwards->begin->index == 5 && backwards->end.index == 6);
    timer->finish_frame();
    timer->poll();
    assert(get_render_task_gpu_timings(state)->status == "pending");
    device->submitted.back()->values = {0, 1000000, 500000, 700000, 3000000, 900, 800};
    device->submitted.back()->ready = true;
    timer->poll();
    const auto measured = get_render_task_gpu_timings(state);
    assert(measured->status == "available" && measured->frame_index == 1);
    assert(measured->tasks.size() == 2 && measured->tasks.front().name == "renamed compute");
    assert(measured->tasks.front().duration_ms == 1 && measured->tasks.front().index == 0);
    assert(measured->tasks.back().duration_ms == 2.5 && measured->tasks.back().index == 1);
    assert(measured->total_duration_ms == 3);
    assert(get_render_task_gpu_timings(state) == measured);
    const auto projection = pal::project_gpu_task_timing_snapshot<std::shared_ptr<int>>(
        measured, [](const auto&) { return std::make_shared<int>(7); });
    assert(pal::project_gpu_task_timing_snapshot<std::shared_ptr<int>>(
               measured, [](const auto&) { return std::make_shared<int>(9); }) == projection);
    timer->task_capacity = 1;
    timer->begin_frame();
    assert(timed_task("first"));
    assert(!timed_task("dropped"));
    timer->finish_frame();
    device->submitted.back()->values = {0, 500000};
    device->submitted.back()->ready = true;
    timer->poll();
    assert(get_render_task_gpu_timings(state)->dropped_task_count == 1);
    assert(get_render_task_gpu_timings(state)->tasks.front().duration_ms == .5);
    // A task whose later pass exceeds the capacity is dropped whole; a frame
    // with every task dropped publishes at once, without a readback.
    const auto readbacks = device->submitted.size();
    timer->begin_frame();
    timer->begin_task();
    assert(timer->pass_timestamps(false) && !timer->pass_timestamps(false));
    timer->end_task("overflow");
    timer->finish_frame();
    assert(device->submitted.size() == readbacks);
    const auto overflowed = get_render_task_gpu_timings(state);
    assert(overflowed->status == "available" && overflowed->frame_index == 3);
    assert(overflowed->tasks.empty() && overflowed->dropped_task_count == 1);
    assert(overflowed->total_duration_ms == 0);
    // An older readback that completes late does not replace a newer frame.
    for (int frame = 0; frame < 2; ++frame) {
        timer->begin_frame();
        assert(timed_task("ordered"));
        timer->finish_frame();
    }
    const auto older = device->submitted.at(device->submitted.size() - 2);
    const auto newer = device->submitted.back();
    newer->values = {0, 250000};
    newer->ready = true;
    timer->poll();
    assert(get_render_task_gpu_timings(state)->frame_index == 5);
    older->values = {0, 750000};
    older->ready = true;
    timer->poll();
    assert(get_render_task_gpu_timings(state)->frame_index == 5);
    assert(get_render_task_gpu_timings(state)->tasks.front().duration_ms == .25);
    for (int frame = 0; frame < 4; ++frame) {
        timer->begin_frame();
        assert(timed_task("backlog"));
        timer->finish_frame();
    }
    assert(timer->in_flight == 4);
    timer->begin_frame();
    assert(!timed_task("skipped"));
    timer->finish_frame();
    assert(timer->in_flight == 4);
    for (const auto& readback : device->submitted)
        readback->fail = true;
    timer->poll();
    assert(timer->in_flight == 0);
    assert(get_render_task_gpu_timings(state)->status == "error");
    assert(get_render_task_gpu_timings(state)->error == "");
    const auto late_publish = timer->publish;
    assert((co_await set_render_task_gpu_timing_enabled(state, false))->status == "disabled");
    late_publish(measured);
    assert(get_render_task_gpu_timings(state)->status == "disabled");
    assert(timer->disposed && !timer->query_set && timer->pending_readbacks.empty());
    timer->dispose();
    assert((co_await set_render_task_gpu_timing_enabled(state, true))->status == "pending");
    late_publish(measured);
    assert(get_render_task_gpu_timings(state)->status == "pending");
    co_await set_render_task_gpu_timing_enabled(state, false);
    loop.close();
    co_return js::PromiseVoid{};
}
} // namespace

int main() {
    const bbl::js::RealmScope realm;
    bbl::pal::EventLoop loop;
    loop.run([&] { check(loop); });
    std::cout << "timing policy passed\n";
}
