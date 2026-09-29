#pragma once
#include <bblite/features/compute_mipmaps.hpp>
#include <bblite/features/compute_shaders.hpp>

#include <bblite/pal_offscreen.hpp>
#include "pal_dawn_resources.hpp"
#include "pal_dawn_gpu_timestamp.hpp"
#if BBLITE_COMPUTE_SHADERS
#include "pal_dawn_compute_pipeline.hpp"
#endif
#if BBLITE_COMPUTE_MIPMAPS
#include "pal_dawn_compute_mipmaps.hpp"
#endif

namespace bbl::pal {
/**
 * Preserve pass boundaries while submitting the source command list once. A
 * source pass's timestamps ride on the native passes it spans: its begin on
 * the first, its end on the last.
 */
inline void submit_dawn_compute_commands(WGPUDevice device, WGPUQueue queue,
                                         std::span<const ComputeCommand> commands) {
    if (commands.empty())
        return;
    DawnCommandEncoder encoder{require_dawn_resource(
        wgpuDeviceCreateCommandEncoder(device, nullptr), "compute command list encoder")};
    const auto timestamp = [&](std::size_t index, bool beginning) -> const GpuTimestampWrite* {
        const auto* write =
            index < commands.size() ? std::get_if<GpuTimestampWrite>(&commands[index]) : nullptr;
        return write && write->beginning == beginning ? write : nullptr;
    };
    const auto native_pass = [&](std::size_t index) {
        return index < commands.size() &&
               !std::holds_alternative<GpuTimestampWrite>(commands[index]);
    };
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];
        if (const auto* write = std::get_if<GpuTimestampWrite>(&command)) {
            if (write->beginning ? !native_pass(index + 1) : index == 0 || !native_pass(index - 1))
                encode_dawn_gpu_timestamp(encoder, *write);
            continue;
        }
        DawnPassTimestamps timestamps;
        if (const auto* begin = index > 0 ? timestamp(index - 1, true) : nullptr)
            timestamps.add(*begin);
        if (const auto* end = timestamp(index + 1, false))
            timestamps.add(*end);
        if (const auto* dispatch = std::get_if<ComputeDispatch>(&command)) {
#if BBLITE_COMPUTE_SHADERS
            encode_dawn_compute(encoder, *dispatch, timestamps.get());
#else
            (void)dispatch;
            throw std::runtime_error("This build does not provide compute dispatch.");
#endif
        } else {
#if BBLITE_COMPUTE_MIPMAPS
            const auto& mip = std::get<ComputeMipmapDraw>(command);
            const auto* level = dynamic_cast<const DawnComputeMipmapLevel*>(mip.level.get());
            if (!level || level->device != device)
                throw std::runtime_error("Mipmap level belongs to a different GPU device.");
            level->encode(encoder, mip.vertices, timestamps.get());
#else
            throw std::runtime_error("This build does not provide compute texture mipmaps.");
#endif
        }
    }
    DawnCommandBuffer submitted{
        require_dawn_resource(wgpuCommandEncoderFinish(encoder, nullptr), "compute command list")};
    submit_dawn_command(queue, submitted);
}
} // namespace bbl::pal
