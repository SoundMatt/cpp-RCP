// cpp-RCP binary version — single source of truth for the CLI and capabilities.
#pragma once

#include <string_view>

namespace rcp {

// Semantic version of the cpp-RCP implementation (matches the latest git tag).
constexpr std::string_view kVersion = "1.0.0";

} // namespace rcp
