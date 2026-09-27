// Shared billboard instance-buffer invalidation.
#pragma once

#include <bblite/runtime.hpp>
#include <array>
#include <cstdint>

namespace bbl::pal {

/** The contents and camera inputs last uploaded by either backend. */
struct BillboardUploadStamp {
    std::array<float, 16> view{};
    std::uint32_t count = 0;
    std::uint64_t instance_version = 0;
    bool uploaded = false;
#if BBLITE_FLOATING_ORIGIN
    Vec3d fo_offset{};
#endif
};

/** Dynamic systems may refill the same count; their version identifies the rows.
 * Cutout systems keep insertion order, so only transparent sorting needs the view. */
inline bool billboard_needs_upload(const BillboardSystemRecord& system,
                                   const BillboardUploadStamp& stamp,
                                   const std::array<float, 16>& view,
                                   [[maybe_unused]] Vec3d fo_offset) {
    if (system.count == 0)
        return false;
    if (!stamp.uploaded || stamp.count != system.count ||
        stamp.instance_version != system.instance_version) {
        return true;
    }
#if BBLITE_FLOATING_ORIGIN
    // The anchors are uploaded eye-relative, including unsorted cutout rows.
    // The pin includes this input through lightFoVersion / wrapRenderableForFO.
    if (stamp.fo_offset.x != fo_offset.x || stamp.fo_offset.y != fo_offset.y ||
        stamp.fo_offset.z != fo_offset.z) {
        return true;
    }
#endif
    const bool cutout = system.depth_mode == BillboardDepthMode::cutout;
    return !(cutout || stamp.view == view);
}

inline void stamp_billboard_upload(BillboardUploadStamp& stamp, const BillboardSystemRecord& system,
                                   const std::array<float, 16>& view,
                                   [[maybe_unused]] Vec3d fo_offset) {
    stamp.view = view;
    stamp.count = system.count;
    stamp.instance_version = system.instance_version;
    stamp.uploaded = true;
#if BBLITE_FLOATING_ORIGIN
    stamp.fo_offset = fo_offset;
#endif
}

} // namespace bbl::pal
