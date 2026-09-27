#include "pal_ui_rml.cpp"
#include <cassert>

namespace bbl::pal {
std::string asset_path(std::string_view) {
    throw std::runtime_error("Unexpected fixture asset read");
}
std::string environment_variable(const char*) { return {}; }
double performance_milliseconds() { return 0; }
} // namespace bbl::pal

int main() {
    using namespace bbl;
    using namespace bbl::pal;
    assert(SDL_Init(SDL_INIT_VIDEO));
    auto* window = SDL_CreateWindow("Canvas stacking fixture", 320, 240, SDL_WINDOW_HIDDEN);
    assert(window);
    {
        Engine engine;
        const auto background = ui_create_element(engine, "div");
        ui_set_attribute(engine, background, "style",
                         "position:absolute;inset:0;background-color:#ff0000;z-index:0;");
        ui_append_to_root(engine, background);
        const auto canvas = ui_create_element(engine, "canvas");
        ui_set_attribute(engine, canvas, "style",
                         "position:absolute;left:20px;top:20px;width:80px;height:80px;z-index:10;");
        ui_canvas_set_width(engine, canvas, 80);
        ui_canvas_set_height(engine, canvas, 80);
        ui_canvas_set_fill_style(engine, canvas, "#0000ff");
        ui_canvas_fill_rect(engine, canvas, 0, 0, 80, 80);
        ui_append_to_root(engine, canvas);
        const auto foreground = ui_create_element(engine, "div");
        ui_set_attribute(
            engine, foreground, "style",
            "position:absolute;left:40px;top:40px;width:40px;height:40px;background-color:#ffff00;z-index:20;");
        ui_append_to_root(engine, foreground);
        UiRmlRuntime runtime(engine, window, 320, 240);
        const auto first_draw = [&](unsigned red, unsigned green, unsigned blue) {
            const auto& frame = record_ui_rml_frame(runtime, 320, 240);
            for (std::size_t index = 0; index < frame.draws.size(); ++index) {
                const auto& draw = frame.draws[index];
                if (!draw.index_count)
                    continue;
                const auto& vertex = frame.vertices.at(frame.indices.at(draw.first_index));
                if (vertex.red == red && vertex.green == green && vertex.blue == blue &&
                    vertex.alpha == 255)
                    return index;
            }
            return frame.draws.size();
        };
        update_ui_rml_runtime(runtime, 320, 240);
        assert(first_draw(255, 0, 0) < first_draw(0, 0, 255));
        assert(first_draw(0, 0, 255) < first_draw(255, 255, 0));
        ui_set_boolean_attribute(engine, canvas, "hidden", true);
        update_ui_rml_runtime(runtime, 320, 240);
        const auto hidden = first_draw(0, 0, 255);
        assert(hidden == record_ui_rml_frame(runtime, 320, 240).draws.size());
        ui_set_boolean_attribute(engine, canvas, "hidden", false);
        ui_set_style_property(engine, canvas, "z-index", "-1");
        update_ui_rml_runtime(runtime, 320, 240);
        assert(first_draw(0, 0, 255) < first_draw(255, 0, 0));
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
}
