#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sns::test {

// Builds the bytes of a minimal TES4 header record, as written by the Creation Kit.
inline std::string make_tes4(std::uint32_t flags, const std::vector<std::string>& masters, float version = 1.71f,
                             const std::string& author = "test") {
  std::string data;
  auto sub = [&](const char* type, const std::string& payload) {
    data.append(type, 4);
    const auto size = static_cast<std::uint16_t>(payload.size());
    data.append(reinterpret_cast<const char*>(&size), 2);
    data.append(payload);
  };
  std::string hedr(12, '\0');
  std::memcpy(hedr.data(), &version, 4);
  sub("HEDR", hedr);
  sub("CNAM", author + '\0');
  for (const auto& m : masters) {
    sub("MAST", m + '\0');
    sub("DATA", std::string(8, '\0'));
  }

  std::string record = "TES4";
  const auto size = static_cast<std::uint32_t>(data.size());
  record.append(reinterpret_cast<const char*>(&size), 4);
  record.append(reinterpret_cast<const char*>(&flags), 4);
  record.append(std::string(12, '\0'));  // form id, version control, form version, unknown
  record.append(data);
  return record;
}

}  // namespace sns::test
