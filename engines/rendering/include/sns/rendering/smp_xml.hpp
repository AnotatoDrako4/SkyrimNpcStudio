#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sns::rendering {

// Parses only metadata useful to the static inspector. This does not execute physics or validate
// the FSMP schema. DTDs and external entities are prohibited; input is capped at 4 MiB.
struct SmpXmlReport {
  bool well_formed = false;
  std::string error;
  std::vector<std::string> collision_shapes;
  std::vector<std::string> bones;
};

SmpXmlReport parse_smp_xml(std::string_view xml);

}  // namespace sns::rendering
