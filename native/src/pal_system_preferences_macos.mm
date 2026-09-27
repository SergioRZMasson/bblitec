#include "pal_system_preferences.hpp"

#import <AppKit/NSAccessibility.h>

namespace bbl::pal {
bool macos_reduced_motion() {
    @autoreleasepool {
        NSWorkspace* workspace = NSWorkspace.sharedWorkspace;
        if (!workspace)
            throw std::runtime_error("Could not read the macOS animation preference.");
        return workspace.accessibilityDisplayShouldReduceMotion;
    }
}
} // namespace bbl::pal
