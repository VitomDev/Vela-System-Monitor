#pragma once

#include <filesystem>

namespace vela {

// Vela stores nothing on disk; settings live in the registry.
std::filesystem::path ExecutablePath();

}  // namespace vela
