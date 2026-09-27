#include "pal_sdl_gpu_canvas.hpp"

#include <map>
#include <stdexcept>
#include <string>

struct SDL_GPUDevice {};
struct SDL_Window {};
struct SDL_GPUTexture {};
static SDL_GPUDevice device;
static SDL_Window window;
static SDL_GPUTexture texture;
static const char* driver = "vulkan";
static bool capability = true, fail_create = false, fail_property = false;
static SDL_PropertiesID next_property = 3;
static std::map<SDL_PropertiesID, std::map<std::string, bool>> properties;
static SDL_GPUTextureCreateInfo last_info;
static bool last_canvas = false;

extern "C" const char* SDLCALL SDL_GetError() { return "fixture failure"; }
extern "C" const char* SDLCALL SDL_GetGPUDeviceDriver(SDL_GPUDevice*) { return driver; }
extern "C" SDL_PropertiesID SDLCALL SDL_GetGPUDeviceProperties(SDL_GPUDevice*) { return 1; }
extern "C" SDL_PropertiesID SDLCALL SDL_GetWindowProperties(SDL_Window*) { return 2; }
extern "C" SDL_PropertiesID SDLCALL SDL_CreateProperties() {
    const auto id = next_property++;
    properties[id] = {};
    return id;
}
extern "C" void SDLCALL SDL_DestroyProperties(SDL_PropertiesID id) { properties.erase(id); }
extern "C" bool SDLCALL SDL_SetBooleanProperty(SDL_PropertiesID id, const char* name, bool value) {
    if (fail_property)
        return false;
    properties[id][name] = value;
    return true;
}
extern "C" bool SDLCALL SDL_GetBooleanProperty(SDL_PropertiesID id, const char* name,
                                               bool fallback) {
    if (id == 1 && std::string(name) == "bblite.gpu.canvas_storage_allocation")
        return capability;
    const auto found = properties.find(id);
    return found != properties.end() && found->second.contains(name) ? found->second.at(name)
                                                                     : fallback;
}
extern "C" SDL_GPUTexture* SDLCALL SDL_CreateGPUTexture(SDL_GPUDevice*,
                                                        const SDL_GPUTextureCreateInfo* info) {
    last_info = *info;
    last_canvas = SDL_GetBooleanProperty(info->props, "bblite.gpu.texture.canvas_storage", false);
    return fail_create ? nullptr : &texture;
}

static void require(bool value) {
    if (!value)
        throw std::runtime_error("SDL canvas allocation contract failed");
}

int main() {
    using namespace bbl::pal;
    require(sdl_canvas_storage_supported(&device, true));
    require(!sdl_canvas_storage_supported(&device, false));
    capability = false;
    require(!sdl_canvas_storage_supported(&device, true));
    capability = true;
    driver = "direct3d12";
    require(!sdl_canvas_storage_supported(&device, true));
    driver = "vulkan";

    const auto usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    const auto create = [&](bool canvas, SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1,
                            std::uint32_t layers = 1) {
        create_frame_texture(&device, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, samples, 128, 64, usage,
                             layers, canvas);
        require(last_info.usage == usage);
        require(last_info.width == 128 && last_info.height == 64);
        require(last_info.sample_count == samples && last_info.layer_count_or_depth == layers);
        require(properties.empty());
    };
    create(false);
    require(!last_canvas && last_info.props == 0);
    create(true);
    require(last_canvas == linux_canvas_storage);
    create(true, SDL_GPU_SAMPLECOUNT_4);
    require(!last_canvas);
    create(true, SDL_GPU_SAMPLECOUNT_1, 2);
    require(!last_canvas && last_info.type == SDL_GPU_TEXTURETYPE_2D_ARRAY);
    capability = false;
    create(true);
    require(!last_canvas);
    capability = true;

    fail_create = true;
    bool refused = false;
    try {
        create(true);
    } catch (const bbl::GpuTransportError&) {
        refused = true;
    }
    require(refused && properties.empty());
    fail_create = false;
    if (linux_canvas_storage) {
        fail_property = true;
        refused = false;
        try {
            create(true);
        } catch (const bbl::GpuTransportError&) {
            refused = true;
        }
        require(refused && properties.empty());
        fail_property = false;
    }

    configure_sdl_canvas_window(&device, &window);
    require(SDL_GetBooleanProperty(2, "bblite.gpu.swapchain.canvas_storage", false) ==
            linux_canvas_storage);
    properties.clear();
    capability = false;
    configure_sdl_canvas_window(&device, &window);
    require(properties.empty());
}
