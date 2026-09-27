#include "pal_gpu_canvas_targets.hpp"

#include <stdexcept>

using namespace bbl;
using namespace bbl::pal;

static void require(bool value) {
    if (!value)
        throw std::runtime_error("Canvas resolve target contract failed.");
}

int main() {
    Engine engine;
    engine.render_targets.resize(8);

    FrameTaskRecord authored;
    authored.kind = FrameTaskKind::render;
    authored.render.target = RenderTargetHandle{0};
    authored.render.resolve_target = RenderTargetHandle{1};
    engine.frame_tasks.push_back(authored);

    FrameTaskRecord scene = authored;
    scene.render.name = "arbitrary-name";
    scene.render.target = RenderTargetHandle{2};
    scene.render.resolve_target = RenderTargetHandle{};
    scene.render.scene_stages = true;
    engine.frame_tasks.push_back(scene);

    FrameTaskRecord resolve;
    resolve.kind = FrameTaskKind::copy;
    resolve.copy.name = "authored-name";
    resolve.copy.source.target = RenderTargetHandle{2};
    resolve.copy.resolve_target = RenderTargetHandle{3};
    engine.frame_tasks.push_back(resolve);

    resolve.copy.source.target = RenderTargetHandle{0};
    resolve.copy.resolve_target = RenderTargetHandle{4};
    engine.frame_tasks.push_back(resolve);

    scene.render.target = RenderTargetHandle{5};
    scene.render.resolve_target = RenderTargetHandle{6};
    engine.frame_tasks.push_back(scene);

    FrameTaskRecord present;
    present.kind = FrameTaskKind::copy;
    present.copy.source.target = RenderTargetHandle{3};
    present.copy.target = RenderTargetHandle{7};
    engine.render_targets[7].swapchain = true;
    engine.frame_tasks.push_back(present);

    const auto roles = canvas_resolve_targets(engine);
    require(roles.size() == 8);
    for (std::size_t index = 0; index < roles.size(); ++index)
        require(roles[index] == (index == 3 || index == 6));
    for (const bool platform : {false, true}) {
        for (const bool supported : {false, true}) {
            require(canvas_resolve_uses_storage(roles[3], supported, platform) ==
                    (supported && platform));
            require(!canvas_resolve_uses_storage(roles[1], supported, platform));
            require(!canvas_resolve_uses_storage(roles[4], supported, platform));
        }
    }
    require(canvas_resolve_uses_storage(true, true) == linux_canvas_storage);
    return 0;
}
