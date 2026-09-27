#pragma once

#include <bblite/runtime.hpp>
#include <vector>

namespace bbl::pal {

/** The readable default pass replaces the pin's canvas resolve with an owned texture. */
inline std::vector<bool> canvas_resolve_targets(const Engine& engine) {
    std::vector<bool> scene_targets(engine.render_targets.size());
    std::vector<bool> resolves(engine.render_targets.size());
    for (const auto& task : engine.frame_tasks) {
        if (task.kind != FrameTaskKind::render || !task.render.scene_stages)
            continue;
        scene_targets.at(task.render.target.value) = true;
        if (task.render.resolve_target.value != invalid_handle)
            resolves.at(task.render.resolve_target.value) = true;
    }
    for (const auto& task : engine.frame_tasks) {
        if (task.kind == FrameTaskKind::copy && task.copy.resolve_target.value != invalid_handle &&
            task.copy.source.source == RenderTextureSource::render_target &&
            scene_targets.at(task.copy.source.target.value)) {
            resolves.at(task.copy.resolve_target.value) = true;
        }
    }
    return resolves;
}

inline constexpr bool linux_canvas_storage =
#if defined(__linux__) && !defined(__ANDROID__)
    true;
#else
    false;
#endif

/** Chromium's Vulkan canvas allocation is storage-capable; matching it preserves MSAA rounding. */
inline bool canvas_resolve_uses_storage(bool canvas_resolve, bool supported,
                                        bool platform = linux_canvas_storage) {
    return platform && canvas_resolve && supported;
}

} // namespace bbl::pal
