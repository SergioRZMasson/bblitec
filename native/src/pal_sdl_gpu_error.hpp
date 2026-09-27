#pragma once

#include <bblite/runtime.hpp>
#include <SDL3/SDL_error.h>
#include <string>

namespace bbl::pal {

[[noreturn]] inline void gpu_error(const char* operation) {
    throw GpuTransportError(std::string(operation) + ": " + SDL_GetError());
}

} // namespace bbl::pal
