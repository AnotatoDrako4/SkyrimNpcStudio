#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sns::mo2 {

// Reader for the INI dialect written by Qt's QSettings (ModOrganizer.ini, meta.ini, settings.ini):
//  - keys are percent-encoded ("Python%20Proxy") and use '\' for sub-groups ("1\binary");
//  - values may be quoted, contain C-style escapes (\\, \", \n, \xHH...) and unquoted commas
//    that separate list items;
//  - typed values are wrapped: @ByteArray(...) holds text, @Variant(...) holds binary data.
// Keys are returned decoded, with '/' as the sub-group separator ("1/binary").
class IniDocument {
 public:
  static IniDocument parse(std::string_view utf8);

  // Decoded value; list items are joined with ','. @Variant values yield nullopt.
  [[nodiscard]] std::optional<std::string> value(std::string_view section, std::string_view key) const;
  [[nodiscard]] std::vector<std::string> list(std::string_view section, std::string_view key) const;
  [[nodiscard]] bool has_section(std::string_view section) const;
  [[nodiscard]] std::vector<std::string> keys(std::string_view section) const;

 private:
  struct Value {
    std::vector<std::string> items;
    bool binary = false;
  };
  // section (case-sensitive like QSettings) -> key -> value
  std::map<std::string, std::map<std::string, Value>, std::less<>> sections_;
};

// Exposed for tests.
std::string decode_ini_key(std::string_view raw);
std::vector<std::string> unescape_ini_value(std::string_view raw);

}  // namespace sns::mo2
