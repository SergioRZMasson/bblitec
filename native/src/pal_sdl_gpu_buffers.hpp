// SDL buffer allocation and upload ownership, shared by every renderer.
#pragma once

#include "pal_sdl_gpu_resources.hpp"
#include "pal_sdl_gpu_writes.hpp"
#include "pal_sdl_gpu_error.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace bbl::pal {

inline void write_sdl_gpu_buffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer, std::size_t offset,
                                 std::span<const std::uint8_t> bytes, bool cycle) {
    if (!device || !buffer)
        throw std::runtime_error("SDL buffer write has no resource.");
    const auto count = gpu_u32(bytes.size()), start = gpu_u32(offset);
    if (count > std::numeric_limits<Uint32>::max() - start)
        throw std::runtime_error("SDL buffer write exceeds the native API size range.");
    if (bytes.empty())
        return;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = count;
    OwnedSdlTransfer transfer{SDL_CreateGPUTransferBuffer(device, &info), {device}};
    if (!transfer)
        gpu_error("SDL_CreateGPUTransferBuffer buffer write");
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), false);
    if (!mapped)
        gpu_error("SDL_MapGPUTransferBuffer buffer write");
    std::memcpy(mapped, bytes.data(), bytes.size());
    SDL_UnmapGPUTransferBuffer(device, transfer.get());
    SdlGpuCommand command{SDL_AcquireGPUCommandBuffer(device)};
    if (!command)
        gpu_error("SDL_AcquireGPUCommandBuffer buffer write");
    SdlCopyPass copy{SDL_BeginGPUCopyPass(command)};
    if (!copy)
        gpu_error("SDL_BeginGPUCopyPass buffer write");
    const SDL_GPUTransferBufferLocation source{transfer.get(), 0};
    const SDL_GPUBufferRegion destination{buffer, start, count};
    SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
    copy.end();
    if (!command.submit())
        gpu_error("SDL_SubmitGPUCommandBuffer buffer write");
}

inline SDL_GPUBuffer* upload_buffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage,
                                    const void* data, std::size_t size) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = gpu_u32(size);
    OwnedSdlBuffer buffer{SDL_CreateGPUBuffer(device, &info), {device}};
    if (!buffer)
        gpu_error("SDL_CreateGPUBuffer");
    if (!data && size)
        throw std::runtime_error("SDL buffer write has no source bytes.");
    SdlBufferDestination destination{device, buffer.get(), false};
    SdlGpuWriteDevice{device}.write_buffer(destination, 0,
                                           {static_cast<const std::uint8_t*>(data), size});
    return buffer.release();
}
inline void update_buffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer, const void* data,
                          std::size_t size) {
    if (!data && size)
        throw std::runtime_error("SDL buffer write has no source bytes.");
    SdlBufferDestination destination{device, buffer, true};
    SdlGpuWriteDevice{device}.write_buffer(destination, 0,
                                           {static_cast<const std::uint8_t*>(data), size});
}

} // namespace bbl::pal
