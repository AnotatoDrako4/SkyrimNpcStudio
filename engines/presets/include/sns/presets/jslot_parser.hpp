#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "sns/presets/preset_model.hpp"
#include "sns/projects/project.hpp"

namespace sns::presets {

PresetItem parse_jslot(std::string_view json_str, std::string default_name = "");

CompatibilityReport evaluate_compatibility(
    const PresetItem& preset,
    const projects::NpcTarget& target,
    const std::string& target_race = "",
    const std::string& target_sex = "",
    const std::vector<std::string>& active_plugins = {});

}  // namespace sns::presets
