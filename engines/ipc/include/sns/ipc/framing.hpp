#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sns::ipc {

// LSP-style message framing:  "Content-Length: <n>\r\n" [other headers] "\r\n" <n bytes of UTF-8 JSON>
// This is what StreamJsonRpc's HeaderDelimitedMessageHandler reads and writes.
std::string encode_frame(std::string_view json);

class FrameDecoder {
 public:
  // Upper bound for a single message; larger frames are treated as protocol corruption.
  static constexpr std::size_t kMaxMessageBytes = 64u * 1024 * 1024;
  static constexpr std::size_t kMaxHeaderBytes = 8u * 1024;

  // Appends raw bytes received from the transport.
  void feed(std::string_view bytes);

  // Returns the next complete message body, if any. Throws sns::Error(Protocol) on malformed headers.
  std::optional<std::string> next();

  [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size() - offset_; }

 private:
  std::string buffer_;
  std::size_t offset_ = 0;
  std::optional<std::size_t> pending_length_;
};

}  // namespace sns::ipc
