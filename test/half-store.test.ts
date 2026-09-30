import test from "node:test";
import {
    optionalNativeFixtureTools,
    runGeneratedProgram,
} from "./native-fixture.js";

const tools = optionalNativeFixtureTools();

test(
    "the rgba16float storage store rounds toward zero",
    { skip: tools ? false : "MSVC fixture tools are unavailable" },
    () => {
        runGeneratedProgram(
            tools!,
            "test-half-store",
            `#include "../../native/src/pal_gpu_images.hpp"
#include <cassert>
#include <cmath>
using bbl::pal::float_to_half;
using bbl::pal::float_to_half_toward_zero;
static double decoded(std::uint16_t half) {
    const int exponent = (half >> 10) & 0x1f, mantissa = half & 0x3ff;
    const double magnitude = exponent == 0 ? std::ldexp(mantissa, -24)
                                           : std::ldexp(1.0 + mantissa / 1024.0, exponent - 15);
    return (half & 0x8000) ? -magnitude : magnitude;
}
int main() {
    // Exactly representable values store unchanged under either rounding.
    for (const float exact : {0.0f, 1.0f, 0.5f, 65504.0f, std::ldexp(1.0f, -24), -2.0f})
        assert(float_to_half_toward_zero(exact) == float_to_half(exact));
    // Where rounding to nearest goes up, the store truncates instead.
    assert(float_to_half(1.0f + 3.0f / 4096.0f) == 0x3c01);
    assert(float_to_half_toward_zero(1.0f + 3.0f / 4096.0f) == 0x3c00);
    assert(float_to_half_toward_zero(std::nextafter(2.0f, 0.0f)) == 0x3fff);
    assert(float_to_half_toward_zero(-std::nextafter(2.0f, 0.0f)) == 0xbfff);
    assert(float_to_half_toward_zero(1.75f * std::ldexp(1.0f, -24)) == 0x0001);
    assert(float_to_half_toward_zero(std::ldexp(1.0f, -26)) == 0x0000);
    // A finite value past the range stays at the largest finite half.
    assert(float_to_half_toward_zero(70000.0f) == 0x7bff);
    assert(float_to_half_toward_zero(INFINITY) == 0x7c00);
    assert(float_to_half_toward_zero(NAN) == 0x7e00);
    // Every stored half is the largest half not above the value.
    for (std::uint32_t bits = 0x33000000u; bits < 0x477fe000u; bits += 997u) {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        const std::uint16_t half = float_to_half_toward_zero(value);
        assert(decoded(half) <= value && value < decoded(static_cast<std::uint16_t>(half + 1)));
    }
}
`,
        );
    },
);
