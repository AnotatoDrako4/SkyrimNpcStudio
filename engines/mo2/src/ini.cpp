#include "sns/mo2/ini.hpp"

#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace {

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// QSettings stores Unicode code points above 0x7F in keys/values as \xHHHH; encode as UTF-8.
void append_code_point(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace

std::string decode_ini_key(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '%' && i + 2 < raw.size() && hex_digit(raw[i + 1]) >= 0 && hex_digit(raw[i + 2]) >= 0) {
      out += static_cast<char>(hex_digit(raw[i + 1]) * 16 + hex_digit(raw[i + 2]));
      i += 2;
    } else if (c == '\\') {
      out += '/';
    } else {
      out += c;
    }
  }
  return out;
}

std::vector<std::string> unescape_ini_value(std::string_view raw) {
  std::vector<std::string> items;
  std::string current;
  bool in_quotes = false;
  bool quoted_item = false;  // quoted text keeps its surrounding spaces

  const auto finish_item = [&] {
    items.push_back(quoted_item ? current : std::string(text::trim(current)));
    current.clear();
    quoted_item = false;
  };

  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '"') {
      in_quotes = !in_quotes;
      quoted_item = true;
      continue;
    }
    if (c == ',' && !in_quotes) {
      finish_item();
      continue;
    }
    if (c == '\\' && i + 1 < raw.size()) {
      const char e = raw[++i];
      switch (e) {
        case 'a': current += '\a'; break;
        case 'b': current += '\b'; break;
        case 'f': current += '\f'; break;
        case 'n': current += '\n'; break;
        case 'r': current += '\r'; break;
        case 't': current += '\t'; break;
        case 'v': current += '\v'; break;
        case '0': current += '\0'; break;
        case 'x': {
          unsigned cp = 0;
          int digits = 0;
          while (i + 1 < raw.size() && digits < 4 && hex_digit(raw[i + 1]) >= 0) {
            cp = cp * 16 + static_cast<unsigned>(hex_digit(raw[++i]));
            ++digits;
          }
          append_code_point(current, cp);
          break;
        }
        default: current += e; break;  // \\ \" \' \? and unknown escapes keep the character
      }
      continue;
    }
    current += c;
  }
  finish_item();
  return items;
}

IniDocument IniDocument::parse(std::string_view utf8) {
  IniDocument doc;
  std::string section = "General";
  for (auto line : text::split_lines(utf8)) {
    line = text::trim(line);
    if (line.empty() || line.front() == ';' || line.front() == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = decode_ini_key(line.substr(1, line.size() - 2));
      doc.sections_[section];
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const auto key = decode_ini_key(text::trim(line.substr(0, eq)));
    auto raw = text::trim(line.substr(eq + 1));

    Value value;
    if (raw.starts_with("@Variant(")) {
      value.binary = true;
    } else {
      value.items = unescape_ini_value(raw);
      if (value.items.size() == 1) {
        auto& s = value.items.front();
        if (s.starts_with("@ByteArray(") && s.ends_with(")")) {
          s = s.substr(11, s.size() - 12);
        } else if (s.starts_with("@@")) {
          s.erase(0, 1);  // QSettings escapes a literal leading '@' as "@@"
        }
      }
    }
    doc.sections_[section][key] = std::move(value);
  }
  return doc;
}

std::optional<std::string> IniDocument::value(std::string_view section, std::string_view key) const {
  const auto s = sections_.find(section);
  if (s == sections_.end()) return std::nullopt;
  const auto k = s->second.find(std::string(key));
  if (k == s->second.end() || k->second.binary) return std::nullopt;
  std::string joined;
  for (size_t i = 0; i < k->second.items.size(); ++i) {
    if (i) joined += ',';
    joined += k->second.items[i];
  }
  return joined;
}

std::vector<std::string> IniDocument::list(std::string_view section, std::string_view key) const {
  const auto s = sections_.find(section);
  if (s == sections_.end()) return {};
  const auto k = s->second.find(std::string(key));
  if (k == s->second.end() || k->second.binary) return {};
  return k->second.items;
}

bool IniDocument::has_section(std::string_view section) const { return sections_.contains(section); }

std::vector<std::string> IniDocument::keys(std::string_view section) const {
  std::vector<std::string> out;
  if (const auto s = sections_.find(section); s != sections_.end()) {
    for (const auto& [k, v] : s->second) out.push_back(k);
  }
  return out;
}

}  // namespace sns::mo2
