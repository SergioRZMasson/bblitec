#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bbl::pal {

/** One installed font face resolved by the host platform's font service. */
struct SystemFontFace {
    std::filesystem::path path;
    std::string family;
    int face_index = 0;
    std::optional<int> named_weight = std::nullopt;
};

/**
 * Resolve an installed face by family, CSS numeric weight and italic style.
 * Generic family names such as "sans-serif" and "monospace" are supported
 * where the platform font service supports them.
 */
std::optional<SystemFontFace> find_system_font(std::string_view family, int weight,
                                               bool italic = false);

} // namespace bbl::pal
