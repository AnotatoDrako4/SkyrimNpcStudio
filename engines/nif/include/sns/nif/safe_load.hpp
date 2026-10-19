#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Forward declaration only: this header is the single door through which first-party code hands NIF bytes
// to nifly (sns_nif and sns_face link nifly privately), so no nifly header leaks into public includes.
namespace nifly {
class NifFile;
}

namespace sns::nif {

inline constexpr std::size_t kMaxNifBytes = 64ull * 1024 * 1024;
// A texture path longer than this is not a Skyrim path (the game's own limit is far lower).
inline constexpr std::uint32_t kMaxTexturePathBytes = 1024;

// Structural check of NIF bytes BEFORE nifly parses them (audit F-10). nifly trusts the header: a block size
// it believes allocates that much (a 238-byte file asked for 4 GB), a string length it believes is read, and
// every texture path goes through std::regex. This refuses, with Error(Parse) naming the defect:
//   - more than kMaxNifBytes, a header that is not Skyrim's (20.2.0.7, user version 12);
//   - any header string or array that runs past the end of the file, a block type index out of range;
//   - block sizes that do not fit in the file (with its footer);
//   - a BSShaderTextureSet whose paths run out of its block or exceed kMaxTexturePathBytes.
// Cost: one pass over the header and the texture-set blocks; no allocation proportional to declared sizes.
void prevalidate_nif(std::string_view bytes, std::string_view label);

// The only way the studio loads NIF bytes into nifly: prevalidate_nif(), then nifly's Load, with every
// exception nifly may throw (std::length_error, std::regex_error, std::bad_alloc...) and every Load failure
// turned into Error(Parse). On return `nif` is valid; on throw it must not be used.
void load_nifly(nifly::NifFile& nif, std::string_view bytes, std::string_view label);

}  // namespace sns::nif
