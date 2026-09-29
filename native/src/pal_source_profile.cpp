// The allocation counters of a `--source-profile` build
// (bblite/source_profile.hpp): the executable's replaceable global
// allocation functions, which count and time every allocation and release
// on the thread making it. The array, sized and nothrow forms reach these
// through their default definitions.
#include <bblite/source_profile.hpp>

#include <cstdlib>
#include <new>

void* operator new(std::size_t size) {
    const std::uint64_t start = bbl::profile::ticks();
    for (;;) {
        if (void* memory = std::malloc(size == 0 ? 1 : size)) {
            bbl::profile::AllocationTotals& totals = bbl::profile::allocation_totals;
            ++totals.allocations;
            totals.bytes += size;
            totals.allocation_ticks += bbl::profile::ticks() - start;
            return memory;
        }
        const std::new_handler handler = std::get_new_handler();
        if (!handler)
            throw std::bad_alloc();
        handler();
    }
}

void operator delete(void* memory) noexcept {
    if (!memory)
        return;
    const std::uint64_t start = bbl::profile::ticks();
    std::free(memory);
    bbl::profile::AllocationTotals& totals = bbl::profile::allocation_totals;
    ++totals.frees;
    totals.allocation_ticks += bbl::profile::ticks() - start;
}
