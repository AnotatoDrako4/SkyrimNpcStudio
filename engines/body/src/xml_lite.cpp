#include "sns/body/xml_lite.hpp"

#include <cstdint>
#include <format>

#include "sns/core/error.hpp"

namespace sns::body {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
 public:
  explicit Parser(std::string_view text) : s_(text) {
    if (s_.starts_with("\xEF\xBB\xBF")) pos_ = 3;
  }

  XmlElement document() {
    skip_misc();
    if (!peek('<')) fail("se esperaba el elemento raíz");
    auto root = element(0);
    skip_misc();
    if (pos_ != s_.size()) fail("contenido después del elemento raíz");
    return root;
  }

 private:
  [[noreturn]] void fail(std::string_view what) const {
    throw Error(ErrorKind::Parse, std::format("XML inválido en el byte {}: {}", pos_, what));
  }
  [[nodiscard]] bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }
  [[nodiscard]] bool peek(std::string_view lit) const { return s_.substr(pos_).starts_with(lit); }
  static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  static bool is_name_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':';
  }
  void skip_space() {
    while (pos_ < s_.size() && is_space(s_[pos_])) ++pos_;
  }
  void skip_until(std::string_view end, std::string_view what) {
    const auto at = s_.find(end, pos_);
    if (at == std::string_view::npos) fail(std::format("{} sin cerrar", what));
    pos_ = at + end.size();
  }
  // Whitespace, comments, <?...?>, <!DOCTYPE ...> between elements.
  void skip_misc() {
    for (;;) {
      skip_space();
      if (peek("<!--")) skip_until("-->", "comentario");
      else if (peek("<?")) skip_until("?>", "instrucción de procesamiento");
      else if (peek("<!DOCTYPE")) skip_until(">", "DOCTYPE");
      else return;
    }
  }
  std::string name() {
    const auto start = pos_;
    while (pos_ < s_.size() && is_name_char(s_[pos_])) ++pos_;
    if (pos_ == start) fail("se esperaba un nombre");
    return std::string(s_.substr(start, pos_ - start));
  }
  static void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  std::string unescape(std::string_view raw) const {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] != '&') {
        out += raw[i];
        continue;
      }
      const auto semi = raw.find(';', i);
      if (semi == std::string_view::npos || semi - i > 12) fail("entidad sin ';'");
      const auto ent = raw.substr(i + 1, semi - i - 1);
      if (ent == "amp") out += '&';
      else if (ent == "lt") out += '<';
      else if (ent == "gt") out += '>';
      else if (ent == "quot") out += '"';
      else if (ent == "apos") out += '\'';
      else if (ent.starts_with('#') && ent.size() > 1) {
        const bool hex = ent[1] == 'x' || ent[1] == 'X';
        const auto digits = ent.substr(hex ? 2 : 1);
        if (digits.empty()) fail("referencia numérica vacía");
        std::uint32_t cp = 0;
        for (const char c : digits) {
          int d = -1;
          if (c >= '0' && c <= '9') d = c - '0';
          else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
          else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
          if (d < 0) fail("referencia numérica inválida");
          cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(d);
          if (cp > 0x10FFFF) fail("referencia numérica fuera de Unicode");
        }
        append_utf8(out, cp);
      } else {
        fail(std::format("entidad desconocida '&{};'", ent));
      }
      i = semi;
    }
    return out;
  }

  XmlElement element(int depth) {
    if (depth > kMaxDepth) fail("anidamiento excesivo");
    ++pos_;  // '<'
    XmlElement e;
    e.name = name();
    for (;;) {
      const bool spaced = pos_ < s_.size() && is_space(s_[pos_]);
      skip_space();
      if (pos_ >= s_.size()) fail("etiqueta sin cerrar");
      if (peek("/>")) {
        pos_ += 2;
        return e;
      }
      if (peek('>')) {
        ++pos_;
        break;
      }
      if (!spaced) fail("falta un espacio entre atributos");
      auto key = name();
      skip_space();
      if (!peek('=')) fail(std::format("atributo '{}' sin '='", key));
      ++pos_;
      skip_space();
      if (!peek('"') && !peek('\'')) fail(std::format("atributo '{}' sin comillas", key));
      const char quote = s_[pos_++];
      const auto end = s_.find(quote, pos_);
      if (end == std::string_view::npos) fail(std::format("valor del atributo '{}' sin cerrar", key));
      const auto raw = s_.substr(pos_, end - pos_);
      if (raw.find('<') != std::string_view::npos) fail(std::format("'<' dentro del atributo '{}'", key));
      for (const auto& [k, _] : e.attributes) {
        if (k == key) fail(std::format("atributo '{}' repetido", key));
      }
      e.attributes.emplace_back(std::move(key), unescape(raw));
      pos_ = end + 1;
    }
    // Content: child elements; text and CDATA are skipped.
    for (;;) {
      const auto lt = s_.find('<', pos_);
      if (lt == std::string_view::npos) fail(std::format("elemento '{}' sin cerrar", e.name));
      pos_ = lt;
      if (peek("</")) {
        pos_ += 2;
        const auto closing = name();
        if (closing != e.name) fail(std::format("se cerró '{}' pero estaba abierto '{}'", closing, e.name));
        skip_space();
        if (!peek('>')) fail("cierre de etiqueta incompleto");
        ++pos_;
        return e;
      }
      if (peek("<!--")) skip_until("-->", "comentario");
      else if (peek("<![CDATA[")) skip_until("]]>", "CDATA");
      else if (peek("<?")) skip_until("?>", "instrucción de procesamiento");
      else e.children.push_back(element(depth + 1));
    }
  }

  std::string_view s_;
  std::size_t pos_ = 0;
};

}  // namespace

std::optional<std::string_view> XmlElement::attribute(std::string_view key) const {
  for (const auto& [k, v] : attributes) {
    if (k == key) return std::string_view(v);
  }
  return std::nullopt;
}

XmlElement parse_xml(std::string_view text) { return Parser(text).document(); }

}  // namespace sns::body
