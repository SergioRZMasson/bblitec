// Backend-specific clear quantization and mip generation.
#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace bbl::pal {

inline SDL_FColor gpu_clear_color(SDL_GPUDevice* device, SDL_GPUTextureFormat format,
                                  SDL_FColor color) {
    if ((format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) &&
        std::strcmp(SDL_GetGPUDeviceDriver(device), "metal") == 0) {
        // Metal fast clears can truncate fractional UNORM values.
        const auto channel = [](float value) {
            return std::nearbyint(std::clamp(value, 0.0f, 1.0f) * 255.0f) / 255.0f;
        };
        return {channel(color.r), channel(color.g), channel(color.b), channel(color.a)};
    }
    return color;
}

inline void generate_texture_mipmaps(SDL_GPUDevice* device, SDL_GPUCommandBuffer* command,
                                     SDL_GPUTexture* texture, std::uint32_t width,
                                     std::uint32_t height, std::uint32_t mip_levels,
                                     std::uint32_t layers = 1) {
    if (mip_levels <= 1)
        return;
    if (std::strcmp(SDL_GetGPUDeviceDriver(device), "metal") != 0) {
        SDL_GenerateMipmapsForGPUTexture(command, texture);
        return;
    }
    // Metal's built-in mip generator averages encoded sRGB. The pinned
    // recordMipmaps samples each preceding level through an sRGB-aware blit.
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        for (std::uint32_t mip = 1; mip < mip_levels; ++mip) {
            SDL_GPUBlitInfo blit{};
            blit.source = {texture,
                           mip - 1,
                           layer,
                           0,
                           0,
                           std::max(1u, width >> (mip - 1)),
                           std::max(1u, height >> (mip - 1))};
            blit.destination = {
                texture, mip, layer, 0, 0, std::max(1u, width >> mip), std::max(1u, height >> mip)};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(command, &blit);
        }
    }
}

} // namespace bbl::pal
