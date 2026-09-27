#include "pal_system_preferences.hpp"

#import <AppKit/NSAccessibility.h>
#import <objc/runtime.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
int calls = 0;
bool next = false;
BOOL read_motion(id, SEL) {
    ++calls;
    return next;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    @autoreleasepool {
        const bool system = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
        require(bbl::pal::macos_reduced_motion() == system, "The PAL disagrees with NSWorkspace");
        const Method method = class_getInstanceMethod(
            NSWorkspace.class, @selector(accessibilityDisplayShouldReduceMotion));
        require(method != nullptr, "The NSWorkspace motion preference is unavailable");
        const IMP previous = method_setImplementation(method, reinterpret_cast<IMP>(read_motion));
        try {
            require(!bbl::pal::system_reduced_motion() && calls == 1,
                    "Initial preference was not read");
            next = true;
            require(!bbl::pal::system_reduced_motion() && calls == 1,
                    "The cached preference changed early");
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            require(bbl::pal::system_reduced_motion() && calls == 2,
                    "Changed preference was not refreshed");
            require(bbl::pal::system_reduced_motion() && calls == 2,
                    "Refreshed value was not cached");
            next = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            require(!bbl::pal::system_reduced_motion() && calls == 3,
                    "Reduced motion did not clear");
        } catch (...) {
            method_setImplementation(method, previous);
            throw;
        }
        method_setImplementation(method, previous);
        require(bbl::pal::macos_reduced_motion() == system, "The system preference changed");
        std::printf(
            "macos-motion-preference-check: system=%d; live preference and cache refresh passed\n",
            system);
    }
}
