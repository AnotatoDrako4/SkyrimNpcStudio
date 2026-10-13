#pragma once
#include <nlohmann/json.hpp>

namespace sns::creation {
void validate_forge_workspace(const nlohmann::json& workspace);
}
