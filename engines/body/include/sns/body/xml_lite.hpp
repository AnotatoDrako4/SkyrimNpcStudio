#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sns::body {

// The minimal XML the BodySlide files need (SliderPresets, SliderGroups): elements, attributes, the five
// predefined entities and numeric character references. Comments, the XML declaration, processing
// instructions, CDATA and DOCTYPE are skipped. Text content is not kept. Malformed input throws
// sns::Error(Parse) with the byte offset; nothing is guessed.
struct XmlElement {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<XmlElement> children;

  [[nodiscard]] std::optional<std::string_view> attribute(std::string_view key) const;  // case-sensitive, like XML
};

// Parses a whole document and returns its root element. Input is UTF-8 (callers decode with text::decode_text).
XmlElement parse_xml(std::string_view text);

}  // namespace sns::body
