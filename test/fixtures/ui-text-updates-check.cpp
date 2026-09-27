#include "pal_ui_rml.cpp"
#include <cassert>

namespace bbl::pal {
std::string asset_path(std::string_view) {
    throw std::runtime_error("Unexpected fixture asset read");
}
std::string environment_variable(const char*) { return {}; }
double performance_milliseconds() { return 0; }
} // namespace bbl::pal

void check_hidden_hud(SDL_Window* window) {
    using namespace bbl;
    using namespace bbl::pal;
    Engine engine;
    const auto root = ui_create_element(engine, "main");
    ui_set_attribute(
        engine, root, "style",
        "position:fixed;inset:0;pointer-events:none;z-index:10;font-family:Verdana,sans-serif;"
        "color:white;");
    ui_append_to_root(engine, root);
    const auto hud = ui_create_element(engine, "div");
    ui_set_attribute(engine, hud, "style",
                     "position:absolute;top:0;right:0;left:0;text-align:center;"
                     "font-effect:glow(0px 8px 3px 3px rgb(17,17,17));");
    ui_set_boolean_attribute(engine, hud, "hidden", true);
    ui_append_child(engine, root, hud);
    const auto counter = ui_create_element(engine, "div");
    ui_set_attribute(engine, counter, "style", "height:30px;font-size:20px;line-height:30px;");
    ui_set_text(engine, counter, "throw 1 / 3");
    ui_append_child(engine, hud, counter);
    const auto score = ui_create_element(engine, "div");
    ui_set_attribute(engine, score, "style",
                     "height:30px;color:#aa0099;font-size:24px;font-weight:bold;line-height:30px;"
                     "font-effect:glow(0px 3px 0px 0px rgb(187,187,187));");
    ui_set_text(engine, score, "0 points");
    ui_append_child(engine, hud, score);
    UiRmlRuntime runtime(engine, window, 640, 480);
    const auto record = [&]() -> const UiRenderFrame& {
        update_ui_rml_runtime(runtime, 640, 480);
        return record_ui_rml_frame(runtime, 640, 480);
    };
    assert(record().draws.empty());
    ui_set_boolean_attribute(engine, hud, "hidden", false);
    const auto& frame = record();
    for (const auto handle : {counter, score}) {
        auto* raw = runtime.projected_elements.at(handle.value).element;
        auto* text = dynamic_cast<Rml::ElementText*>(raw->GetChild(0));
        assert(text && text->GetFontFaceHandle());
        assert(text->GetText() == (handle == counter ? "throw 1 / 3" : "0 points"));
    }
    const auto visible_glyphs = [&](std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                    float top, float bottom) {
        std::size_t count = 0;
        for (const auto& draw : frame.draws) {
            if (!draw.texture_id || !draw.scissor_width || !draw.scissor_height)
                continue;
            const auto texture =
                std::find_if(frame.textures.begin(), frame.textures.end(),
                             [&](const auto& value) { return value.id == draw.texture_id; });
            assert(texture != frame.textures.end() && texture->rgba);
            if (std::none_of(texture->rgba->begin(), texture->rgba->end(),
                             [](auto value) { return value != 0; }))
                continue;
            for (std::size_t i = draw.first_index; i < draw.first_index + draw.index_count; ++i) {
                const auto& vertex = frame.vertices.at(frame.indices.at(i));
                if (vertex.red == red && vertex.green == green && vertex.blue == blue &&
                    vertex.alpha > 0 && vertex.x > 200 && vertex.x < 440 && vertex.y >= top &&
                    vertex.y <= bottom)
                    ++count;
            }
        }
        return count;
    };
    assert(visible_glyphs(255, 255, 255, 0, 30) >= 12);
    assert(visible_glyphs(170, 0, 153, 30, 60) >= 12);

    ui_set_text(engine, counter, "throw 2 / 3");
    ui_set_text(engine, score, "120 points");
    ui_set_style_property(engine, root, "font-family", "'Not,a real font', 'Verdana', sans-serif");
    static_cast<void>(record());
    assert(visible_glyphs(255, 255, 255, 0, 30) >= 12);
    assert(visible_glyphs(170, 0, 153, 30, 60) >= 12);
    ui_set_boolean_attribute(engine, hud, "hidden", true);
    assert(record().draws.empty());
    ui_set_boolean_attribute(engine, hud, "hidden", false);
    static_cast<void>(record());
    assert(visible_glyphs(255, 255, 255, 0, 30) >= 12);
    assert(visible_glyphs(170, 0, 153, 30, 60) >= 12);
}

int main() {
    using namespace bbl;
    using namespace bbl::pal;
    assert(SDL_Init(SDL_INIT_VIDEO));
    auto* window = SDL_CreateWindow("Text update fixture", 640, 480, SDL_WINDOW_HIDDEN);
    assert(window);
    {
        Engine engine;
        const auto parent = ui_create_element(engine, "div");
        ui_set_attribute(engine, parent, "style", "font-size:16px;display:block;");
        ui_append_to_root(engine, parent);
        const auto leaf = [&](const char* style) {
            const auto handle = ui_create_element(engine, "span");
            ui_set_attribute(engine, handle, "style", style);
            ui_set_text(engine, handle, "Short");
            ui_append_child(engine, parent, handle);
            return handle;
        };
        const auto plain = leaf("display:inline-block;");
        const auto flex = leaf("display:flex;");
        const auto grid = leaf("display:grid;grid-template-columns:1fr;");
        const auto outlined = leaf("display:inline-block;--bbl-outline:1px solid red;");
        UiRmlRuntime runtime(engine, window, 640, 480);
        const auto update = [&] { update_ui_rml_runtime(runtime, 640, 480); };
        const auto raw = [&](UiElementHandle handle) {
            return runtime.projected_elements.at(handle.value).element;
        };
        const auto text = [&](UiElementHandle handle) {
            auto* element = raw(handle)->GetChild(0);
            if (runtime.projected_elements.at(handle.value).text_wrapped)
                element = element->GetChild(0);
            auto* result = dynamic_cast<Rml::ElementText*>(element);
            assert(result);
            return result;
        };
        update();
        auto* plain_text = text(plain);
        auto* flex_text = text(flex);
        auto* grid_text = text(grid);
        auto* flex_wrapper = raw(flex)->GetChild(0);
        auto* grid_wrapper = raw(grid)->GetChild(0);
        const auto initial_width = raw(plain)->GetBox().GetSize().x;
        ui_set_text(engine, plain, "A much longer plain label");
        ui_set_text(engine, flex, "Changed flex label");
        ui_set_text(engine, grid, "Changed grid label");
        update();
        assert(text(plain) == plain_text && text(flex) == flex_text && text(grid) == grid_text);
        assert(raw(flex)->GetChild(0) == flex_wrapper && raw(grid)->GetChild(0) == grid_wrapper);
        assert(plain_text->GetText() == "A much longer plain label");
        assert(raw(plain)->GetBox().GetSize().x > initial_width * 2);
        assert(raw(outlined)->QuerySelector("bbl-outline"));
        static_cast<void>(record_ui_rml_frame(runtime, 640, 480));
        ui_set_style_property(engine, plain, "font-family", "system-ui");
        ui_set_style_property(engine, plain, "font-weight", "600");
        update();
        const auto single_face = text(plain)->GetFontFaceHandle();
        const auto single_width = raw(plain)->GetBox().GetSize().x;
        ui_set_style_property(engine, parent, "font-family", "monospace");
        ui_set_attribute(engine, plain, "style",
                         "display:inline-block;font-weight:600;"
                         "font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;");
        update();
        assert(text(plain)->GetFontFaceHandle() == single_face);
        assert(raw(plain)->GetBox().GetSize().x == single_width);

        // An inherited font change in the same batch must still project style.
        const auto previous_width = raw(plain)->GetBox().GetSize().x;
        ui_set_text(engine, plain, "A much longer plain label!");
        ui_set_attribute(engine, parent, "style", "font-size:32px;display:block;");
        assert(!engine.ui_only_text_changed_since(runtime.projected_revision,
                                                  runtime.projected_text_revision));
        update();
        assert(raw(plain)->GetComputedValues().font_size() == 32);
        assert(raw(plain)->GetBox().GetSize().x > previous_width * 1.8f);

        // Empty and whitespace transitions may change :empty and wrapper display.
        ui_set_text(engine, plain, "");
        assert(!engine.ui_only_text_changed_since(runtime.projected_revision,
                                                  runtime.projected_text_revision));
        update();
        assert(raw(plain)->Matches(":empty"));
        ui_set_text(engine, plain, "Restored");
        ui_set_text(engine, flex, " ");
        update();
        assert(!raw(plain)->Matches(":empty"));
        assert(raw(flex)->GetChild(0)->GetComputedValues().display() == Rml::Style::Display::None);
        ui_set_text(engine, flex, "Visible");
        update();
        assert(raw(flex)->GetChild(0)->GetComputedValues().display() != Rml::Style::Display::None);

        // Emoji presentation needs the existing markup projection.
        const std::string emoji = "Heart \xe2\x9d\xa4\xef\xb8\x8f";
        assert(ui_text_needs_emoji_normalization(emoji));
        ui_set_text(engine, plain, emoji);
        assert(!engine.ui_only_text_changed_since(runtime.projected_revision,
                                                  runtime.projected_text_revision));
        update();
        assert(raw(plain)->GetChild(0)->GetTagName() == "span");
        static_cast<void>(record_ui_rml_frame(runtime, 640, 480));
    }
    check_hidden_hud(window);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
