#include <cassert>
#include <exception>
#include <memory>
#include <stdexcept>
#include <variant>

#include "allocation-tracker.hpp"
#include "load-promise.hpp"

struct Rejection : std::runtime_error {
    explicit Rejection(std::shared_ptr<int> token)
        : std::runtime_error("retained rejection"), identity(std::move(token)) {}
    std::shared_ptr<int> identity;
};

int main() {
    const auto before = allocation_count;
    GltfLoadPromise<int> value{42};
    auto copy = value;
    value = {};
    assert(copy && copy.get() == 42);
    assert(allocation_count == before);
    auto image = std::make_shared<int>(7);
    const auto image_allocations = allocation_count;
    auto settled = GltfLoadPromise<std::shared_ptr<int>>::settle([&] { return image; });
    auto shared = settled;
    settled = {};
    assert(shared.get() == image);
    assert(allocation_count == image_allocations);
    const auto identity = std::make_shared<int>(9);
    const auto failure = std::make_exception_ptr(Rejection(identity));
    auto rejected = GltfLoadPromise<int>::settle([&]() -> int { std::rethrow_exception(failure); });
    const auto error_allocations = allocation_count;
    auto retained = rejected;
    rejected = {};
    assert(allocation_count == error_allocations);
    bool caught = false;
    try {
        (void)retained.get();
    } catch (const Rejection& error) {
        assert(error.identity == identity);
        caught = true;
    }
    assert(caught);
    bool absent = false;
    try {
        (void)value.get();
    } catch (const std::runtime_error&) {
        absent = true;
    }
    assert(absent);
    // A prepared product is taken once: its value moves out, or its rejection
    // is raised where it is taken, and the promise is then absent.
    auto owned = GltfLoadPromise<std::shared_ptr<int>>::settle([&] { return image; });
    assert(owned.fulfilled() && !retained.fulfilled() && !value.fulfilled());
    assert(owned.take() == image && !owned && !owned.fulfilled());
    bool taken_rejection = false;
    try {
        (void)retained.take();
    } catch (const Rejection& error) {
        taken_rejection = error.identity == identity;
    }
    assert(taken_rejection);
}
