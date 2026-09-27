#include "pal_image.cpp"
#include <cassert>
#include <future>
#include "image.hpp"

namespace bbl::pal {
std::string environment_variable(const char*) { return {}; }
} // namespace bbl::pal

int main() {
    const bbl::js::ArrayBuffer input(png_bytes);
#if BBLITE_HAS_IMAGE_DECODER
    for (const auto& fixture : png16_cases) {
        SDL_Surface* surface =
            IMG_Load_IO(SDL_IOFromConstMem(fixture.bytes.data(), fixture.bytes.size()), true);
        assert(surface && surface->w == 3 && surface->h == 2);
        assert(surface->format == (fixture.alpha ? SDL_PIXELFORMAT_RGBA64 : SDL_PIXELFORMAT_RGB48));
        const std::size_t row_samples = fixture.alpha ? 12 : 9;
        // Three RGB48 pixels leave row padding; sample checks must cross it.
        assert(fixture.alpha || surface->pitch > static_cast<int>(row_samples * sizeof(Uint16)));
        for (int y = 0; y < surface->h; ++y) {
            const auto* row = reinterpret_cast<const Uint16*>(
                static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
            assert(std::equal(row, row + row_samples, fixture.samples.begin() + y * row_samples));
        }
        SDL_DestroySurface(surface);
        const auto image = bbl::pal::decode_image(std::span<const std::uint8_t>{fixture.bytes});
        assert(image.width == 3 && image.height == 2 && image.rgba == fixture.rgba);
    }
    std::vector<std::future<bbl::pal::DecodedImage>> native_decodes;
    for (int index = 0; index < 8; ++index)
        native_decodes.push_back(std::async(std::launch::async, [] {
            return bbl::pal::decode_image(std::span<const std::uint8_t>{png_bytes});
        }));
    for (auto& result : native_decodes) {
        const auto image = result.get();
        assert(image.width == 3 && image.height == 2 && image.rgba == expected_pixels);
    }
    const auto decoded = bbl::pal::decode_image(input);
    assert(decoded.width == 3 && decoded.height == 2);
    assert(decoded.rgba == expected_pixels);
    assert(input.byte_length() == png_bytes.size());
    assert(std::equal(png_bytes.begin(), png_bytes.end(), input.data()));
    for (const auto& bytes :
         {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{1, 2, 3},
          std::vector<std::uint8_t>(png_bytes.begin(), png_bytes.begin() + 16)}) {
        try {
            static_cast<void>(bbl::pal::decode_image(bbl::js::ArrayBuffer(bytes)));
            assert(false);
        } catch (const std::runtime_error& error) {
            const std::string_view message(error.what());
            assert(message.starts_with("Unable to open image:") ||
                   message.starts_with("Unable to decode image:"));
        }
    }
#else
    try {
        static_cast<void>(bbl::pal::decode_image(input));
        assert(false);
    } catch (const std::runtime_error& error) {
        assert(std::string_view(error.what()) == "This scene was built without image decoding.");
    }
#endif
}
