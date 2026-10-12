#include "sns/ipc/framing.hpp"

#include <charconv>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::ipc {

std::string encode_frame(std::string_view json) {
  std::string out = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n";
  out.append(json);
  return out;
}

void FrameDecoder::feed(std::string_view bytes) {
  // Compact consumed bytes occasionally so the buffer does not grow without bound.
  if (offset_ > 0 && offset_ >= buffer_.size() / 2) {
    buffer_.erase(0, offset_);
    offset_ = 0;
  }
  buffer_.append(bytes);
}

std::optional<std::string> FrameDecoder::next() {
  if (!pending_length_) {
    const auto view = std::string_view(buffer_).substr(offset_);
    const auto end = view.find("\r\n\r\n");
    if (end == std::string_view::npos) {
      if (view.size() > kMaxHeaderBytes) throw Error(ErrorKind::Protocol, "message header too large");
      return std::nullopt;
    }

    std::optional<std::size_t> length;
    for (auto line : text::split_lines(view.substr(0, end))) {
      const auto colon = line.find(':');
      if (colon == std::string_view::npos) throw Error(ErrorKind::Protocol, "malformed message header");
      const auto name = text::trim(line.substr(0, colon));
      const auto value = text::trim(line.substr(colon + 1));
      if (text::iequals_ascii(name, "Content-Length")) {
        std::size_t n = 0;
        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
        if (ec != std::errc{} || ptr != value.data() + value.size()) {
          throw Error(ErrorKind::Protocol, "invalid Content-Length");
        }
        length = n;
      }
      // Other headers (e.g. Content-Type) are allowed and ignored.
    }
    if (!length) throw Error(ErrorKind::Protocol, "missing Content-Length header");
    if (*length > kMaxMessageBytes) throw Error(ErrorKind::Protocol, "message exceeds the maximum size");
    pending_length_ = length;
    offset_ += end + 4;
  }

  if (buffer_.size() - offset_ < *pending_length_) return std::nullopt;
  std::string body = buffer_.substr(offset_, *pending_length_);
  offset_ += *pending_length_;
  pending_length_.reset();
  return body;
}

}  // namespace sns::ipc
