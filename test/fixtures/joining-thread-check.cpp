#include <bblite/joining_thread.hpp>
#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
    int completed = 0;
    try {
        bbl::JoiningThread thread(
            [owned = std::make_unique<int>(7), &completed] { completed = *owned; });
        throw std::runtime_error("unwind");
    } catch (const std::runtime_error&) {
    }
    assert(completed == 7);
    std::atomic<int> cancelled = 0;
    auto work = [&cancelled](bbl::StopToken stop) {
        while (!stop.stop_requested())
            std::this_thread::yield();
        ++cancelled;
    };
    bbl::JoiningThread empty;
    empty.request_stop();
    assert(!empty.get_stop_token().stop_requested());
    {
        bbl::JoiningThread first(work);
        const auto token = first.get_stop_token();
        const auto id = first.get_id();
        bbl::JoiningThread second(std::move(first));
        first.request_stop();
        assert(!token.stop_requested());
        assert(second.get_id() == id);
        second = bbl::JoiningThread(work);
        assert(token.stop_requested() && cancelled == 1);
        second.request_stop();
        assert(second.get_stop_token().stop_requested());
    }
    assert(cancelled == 2);
    {
        std::vector<bbl::JoiningThread> workers;
        for (int i = 0; i != 8; ++i)
            workers.emplace_back(work);
    }
    assert(cancelled == 10);
}
