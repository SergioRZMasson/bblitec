#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::atomic<int> validation_errors = 0;
void SDLCALL log_message(void*, int, SDL_LogPriority priority, const char* message) {
    std::fprintf(stderr, "%s\n", message);
    if (priority >= SDL_LOG_PRIORITY_ERROR ||
        std::string_view(message).find("Validation layers not found") != std::string_view::npos)
        ++validation_errors;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}
template <typename T, auto Release> struct Deleter {
    SDL_GPUDevice* device;
    void operator()(T* pointer) const { Release(device, pointer); }
};
template <typename T, auto Release> using Owner = std::unique_ptr<T, Deleter<T, Release>>;
using Texture = Owner<SDL_GPUTexture, SDL_ReleaseGPUTexture>;
using Transfer = Owner<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer>;
using Buffer = Owner<SDL_GPUBuffer, SDL_ReleaseGPUBuffer>;
using Pipeline = Owner<SDL_GPUComputePipeline, SDL_ReleaseGPUComputePipeline>;
using Sampler = Owner<SDL_GPUSampler, SDL_ReleaseGPUSampler>;
using Pixel = std::array<float, 4>;
constexpr Uint32 width = 4;
constexpr Uint32 stride = 16; // A 256-byte readback row for RGBA32F.
constexpr Uint32 plane_bytes = stride * width * sizeof(Pixel);

SDL_GPUCommandBuffer* command(SDL_GPUDevice* device) {
    auto* result = SDL_AcquireGPUCommandBuffer(device);
    require(result, "Acquire command");
    return result;
}
void submit(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands) {
    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    require(fence, "Submit command");
    const bool complete = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    require(complete, "Wait for GPU");
}
Transfer transfer(SDL_GPUDevice* device, SDL_GPUTransferBufferUsage usage, Uint32 size) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    Transfer result{SDL_CreateGPUTransferBuffer(device, &info), {device}};
    require(bool(result), "Create transfer");
    return result;
}
Texture texture(SDL_GPUDevice* device, SDL_GPUTextureType type, Uint32 layers, Uint32 mips,
                bool array) {
    SDL_GPUTextureCreateInfo info{};
    info.type = type;
    info.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
                 SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
                 SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
    info.width = width;
    info.height = width;
    info.layer_count_or_depth = layers;
    info.num_levels = mips;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    require(SDL_GPUTextureSupportsFormat(device, info.format, info.type, info.usage),
            "Texture format");
    info.props = SDL_CreateProperties();
    require(info.props &&
                SDL_SetBooleanProperty(info.props, "bblite.gpu.texture.storage_array", array),
            "Texture properties");
    Texture result{SDL_CreateGPUTexture(device, &info), {device}};
    // The backend must retain the property for pass barriers, cycling and views.
    SDL_DestroyProperties(info.props);
    require(bool(result), "Create texture");
    return result;
}
Pipeline pipeline(SDL_GPUDevice* device, const std::filesystem::path& directory, const char* name,
                  bool write, bool read = false, bool sampled = false) {
    std::ifstream file(directory / (std::string(name) + ".spv"), std::ios::binary);
    require(bool(file), "Read shader");
    const std::vector<Uint8> bytes{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
    SDL_GPUComputePipelineCreateInfo info{};
    info.code = bytes.data();
    info.code_size = bytes.size();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_readwrite_storage_textures = write ? 1 : 0;
    info.num_uniform_buffers = write ? 1 : 0;
    info.num_readonly_storage_textures = read ? 1 : 0;
    info.num_samplers = sampled ? 1 : 0;
    info.num_readwrite_storage_buffers = write ? 0 : 1;
    info.threadcount_x = info.threadcount_y = info.threadcount_z = 1;
    Pipeline result{SDL_CreateGPUComputePipeline(device, &info), {device}};
    require(bool(result), "Create pipeline");
    return result;
}
float sentinel(Uint32 layer, Uint32 mip) {
    return 1000.0f + 100.0f * static_cast<float>(mip) + static_cast<float>(layer);
}
void initialize(SDL_GPUDevice* device, SDL_GPUTexture* image, Uint32 layers, Uint32 mips) {
    auto staging =
        transfer(device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, layers * mips * plane_bytes);
    auto* data = static_cast<Pixel*>(SDL_MapGPUTransferBuffer(device, staging.get(), false));
    require(data, "Map upload");
    for (Uint32 layer = 0; layer < layers; ++layer)
        for (Uint32 mip = 0; mip < mips; ++mip)
            for (Uint32 pixel = 0; pixel < stride * width; ++pixel)
                data[(layer * mips + mip) * stride * width + pixel] = {sentinel(layer, mip), 2, 3,
                                                                       1};
    SDL_UnmapGPUTransferBuffer(device, staging.get());
    auto* commands = command(device);
    auto* pass = SDL_BeginGPUCopyPass(commands);
    require(pass, "Begin upload");
    for (Uint32 layer = 0; layer < layers; ++layer) {
        for (Uint32 mip = 0; mip < mips; ++mip) {
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = staging.get();
            source.offset = (layer * mips + mip) * plane_bytes;
            source.pixels_per_row = stride;
            source.rows_per_layer = width;
            SDL_GPUTextureRegion target{};
            target.texture = image;
            target.layer = layer;
            target.mip_level = mip;
            target.w = target.h = width >> mip;
            target.d = 1;
            SDL_UploadToGPUTexture(pass, &source, &target, false);
        }
    }
    SDL_EndGPUCopyPass(pass);
    submit(device, commands);
}
void write(SDL_GPUCommandBuffer* commands, SDL_GPUComputePipeline* program, SDL_GPUTexture* image,
           Uint32 layer, Uint32 mip, Uint32 layers, Pixel value, bool cycle = false,
           SDL_GPUDevice* release_before_end = nullptr) {
    SDL_GPUStorageTextureReadWriteBinding binding{};
    binding.texture = image;
    binding.layer = layer;
    binding.mip_level = mip;
    binding.cycle = cycle;
    auto* pass = SDL_BeginGPUComputePass(commands, &binding, 1, nullptr, 0);
    require(pass, "Begin write");
    SDL_BindGPUComputePipeline(pass, program);
    SDL_PushGPUComputeUniformData(commands, 0, value.data(), sizeof(value));
    SDL_DispatchGPUCompute(pass, width >> mip, width >> mip, layers);
    if (release_before_end)
        SDL_ReleaseGPUTexture(release_before_end, image);
    SDL_EndGPUComputePass(pass);
}
Transfer download(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* image,
                  Uint32 layers, Uint32 mips) {
    auto result =
        transfer(device, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, layers * mips * plane_bytes);
    auto* pass = SDL_BeginGPUCopyPass(commands);
    require(pass, "Begin download");
    for (Uint32 layer = 0; layer < layers; ++layer) {
        for (Uint32 mip = 0; mip < mips; ++mip) {
            SDL_GPUTextureRegion source{};
            source.texture = image;
            source.layer = layer;
            source.mip_level = mip;
            source.w = source.h = width >> mip;
            source.d = 1;
            SDL_GPUTextureTransferInfo target{};
            target.transfer_buffer = result.get();
            target.offset = (layer * mips + mip) * plane_bytes;
            target.pixels_per_row = stride;
            target.rows_per_layer = width;
            SDL_DownloadFromGPUTexture(pass, &source, &target);
        }
    }
    SDL_EndGPUCopyPass(pass);
    return result;
}
template <typename Expected>
void check(SDL_GPUDevice* device, SDL_GPUTransferBuffer* result, Uint32 layers, Uint32 mips,
           Expected expected) {
    const auto* data = static_cast<const Pixel*>(SDL_MapGPUTransferBuffer(device, result, false));
    require(data, "Map download");
    for (Uint32 layer = 0; layer < layers; ++layer)
        for (Uint32 mip = 0; mip < mips; ++mip)
            for (Uint32 y = 0; y < (width >> mip); ++y)
                for (Uint32 x = 0; x < (width >> mip); ++x)
                    require(data[(layer * mips + mip) * stride * width + y * stride + x] ==
                                Pixel{expected(layer, mip), 2, 3, 1},
                            "Layer/mip texel mismatch");
    SDL_UnmapGPUTransferBuffer(device, result);
}
void read_views(SDL_GPUDevice* device, SDL_GPUComputePipeline* program, SDL_GPUTexture* image,
                Uint32 layers, float first, SDL_GPUSampler* sampler = nullptr) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    info.size = layers * sizeof(Pixel);
    Buffer buffer{SDL_CreateGPUBuffer(device, &info), {device}};
    require(bool(buffer), "Create shader output");
    auto result = transfer(device, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, info.size);
    auto* commands = command(device);
    SDL_GPUStorageBufferReadWriteBinding binding{};
    binding.buffer = buffer.get();
    auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1);
    require(pass, "Begin read");
    SDL_BindGPUComputePipeline(pass, program);
    if (sampler) {
        const SDL_GPUTextureSamplerBinding sampled{image, sampler};
        SDL_BindGPUComputeSamplers(pass, 0, &sampled, 1);
    } else {
        SDL_BindGPUComputeStorageTextures(pass, 0, &image, 1);
    }
    SDL_DispatchGPUCompute(pass, layers, 1, 1);
    SDL_EndGPUComputePass(pass);
    auto* copy = SDL_BeginGPUCopyPass(commands);
    require(copy, "Begin buffer download");
    const SDL_GPUBufferRegion region{buffer.get(), 0, info.size};
    const SDL_GPUTransferBufferLocation destination{result.get(), 0};
    SDL_DownloadFromGPUBuffer(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);
    submit(device, commands);
    const auto* data =
        static_cast<const Pixel*>(SDL_MapGPUTransferBuffer(device, result.get(), false));
    require(data, "Map shader output");
    for (Uint32 layer = 0; layer < layers; ++layer)
        require(data[layer] == Pixel{first + static_cast<float>(layer), 2, 3, 1},
                "Storage/sampler view mismatch");
    SDL_UnmapGPUTransferBuffer(device, result.get());
}
void exercise(SDL_GPUDevice* device, const std::filesystem::path& directory) {
    auto single = pipeline(device, directory, "write2d", true);
    auto array = pipeline(device, directory, "writeArray", true);
    auto add = pipeline(device, directory, "addArray", true);
    auto read = pipeline(device, directory, "readArray", false, true);
    auto sample = pipeline(device, directory, "sampleCube", false, false, true);
    {
        auto image = texture(device, SDL_GPU_TEXTURETYPE_2D_ARRAY, 3, 2, false);
        initialize(device, image.get(), 3, 2);
        auto* commands = command(device);
        write(commands, single.get(), image.get(), 1, 1, 1, {100, 2, 3, 1});
        auto result = download(device, commands, image.get(), 3, 2);
        submit(device, commands);
        check(device, result.get(), 3, 2, [](Uint32 layer, Uint32 mip) {
            return layer == 1 && mip == 1 ? 100.0f : sentinel(layer, mip);
        });
        std::puts("default layer/mip isolation passed");
    }
    {
        auto image = texture(device, SDL_GPU_TEXTURETYPE_2D_ARRAY, 3, 2, true);
        initialize(device, image.get(), 3, 2);
        auto* commands = command(device);
        write(commands, array.get(), image.get(), 0, 1, 3, {200, 2, 3, 1});
        write(commands, add.get(), image.get(), 0, 1, 3, {10, 0, 0, 0});
        write(commands, single.get(), image.get(), 2, 0, 1, {300, 2, 3, 1});
        auto result = download(device, commands, image.get(), 3, 2);
        submit(device, commands);
        check(device, result.get(), 3, 2, [](Uint32 layer, Uint32 mip) {
            return mip == 1     ? 210.0f + static_cast<float>(layer)
                   : layer == 2 ? 300.0f
                                : sentinel(layer, mip);
        });
        std::puts("whole-array mip and read/write barriers passed");
        std::puts("opt-in nonzero layer isolation passed");
        commands = command(device);
        write(commands, array.get(), image.get(), 0, 0, 3, {400, 2, 3, 1});
        auto before_cycle = download(device, commands, image.get(), 3, 1);
        // The prior pass retains this allocation, forcing a new cycled texture.
        write(commands, array.get(), image.get(), 0, 0, 3, {500, 2, 3, 1}, true);
        auto after_cycle = download(device, commands, image.get(), 3, 1);
        submit(device, commands);
        check(device, before_cycle.get(), 3, 1,
              [](Uint32 layer, Uint32) { return 400.0f + static_cast<float>(layer); });
        check(device, after_cycle.get(), 3, 1,
              [](Uint32 layer, Uint32) { return 500.0f + static_cast<float>(layer); });
        read_views(device, read.get(), image.get(), 3, 500);
        std::puts("cycling preserves owned view properties and prior allocation passed");
    }
    for (const bool cube : {false, true}) {
        const Uint32 layers = cube ? 6 : 1;
        auto image = texture(device, cube ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_2D,
                             layers, 1, true);
        auto* commands = command(device);
        write(commands, array.get(), image.get(), 0, 0, layers, {600, 2, 3, 1});
        submit(device, commands);
        read_views(device, read.get(), image.get(), layers, 600);
        std::puts(cube ? "cube readonly array alias passed"
                       : "single-layer readonly array alias passed");
        if (cube) {
            SDL_GPUSamplerCreateInfo info{};
            Sampler sampler{SDL_CreateGPUSampler(device, &info), {device}};
            require(bool(sampler), "Create sampler");
            read_views(device, sample.get(), image.get(), layers, 600, sampler.get());
            std::puts("sampled cube view preserved passed");
        }
    }
    {
        auto image = texture(device, SDL_GPU_TEXTURETYPE_2D_ARRAY, 3, 1, true);
        auto* commands = command(device);
        write(commands, array.get(), image.release(), 0, 0, 3, {700, 2, 3, 1}, false, device);
        submit(device, commands);
        std::puts("released container end-of-pass lifetime passed");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Shader directory argument");
        SDL_SetLogOutputFunction(log_message, nullptr);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
        SDL_GPUVulkanOptions vulkan{};
        // Pinned Tint emits SPIR-V 1.3, which requires Vulkan 1.1 semantics.
        vulkan.vulkan_api_version = (1u << 22) | (1u << 12);
        const auto properties = SDL_CreateProperties();
        require(properties &&
                    SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,
                                          "vulkan") &&
                    SDL_SetBooleanProperty(
                        properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) &&
                    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
                                           true) &&
                    SDL_SetPointerProperty(
                        properties, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vulkan),
                "Device properties");
        std::unique_ptr<SDL_GPUDevice, decltype(&SDL_DestroyGPUDevice)> device{
            SDL_CreateGPUDeviceWithProperties(properties), SDL_DestroyGPUDevice};
        SDL_DestroyProperties(properties);
        require(bool(device), "Vulkan device");
        require(SDL_GetBooleanProperty(SDL_GetGPUDeviceProperties(device.get()),
                                       "bblite.gpu.storage_texture_array", false),
                "Maintained storage-array capability");
        exercise(device.get(), argv[1]);
        device.reset();
        SDL_Quit();
        require(validation_errors.load() == 0,
                "Vulkan validation errors or unavailable validation layer");
        std::puts("sdl-storage-array-check: 8 cases passed with Vulkan validation");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        SDL_Quit();
        return 1;
    }
}
