#pragma once

#include <stdexcept>
#include <string>

namespace sns {

enum class ErrorKind {
  Io,
  Parse,
  NotFound,
  InvalidArgument,
  Protocol,
  Timeout,
  Cancelled,
  Remote,
};

// Base exception of every engine. Messages are UTF-8 and meant to be shown to users.
class Error : public std::runtime_error {
 public:
  Error(ErrorKind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
  [[nodiscard]] ErrorKind kind() const noexcept { return kind_; }

 private:
  ErrorKind kind_;
};

}  // namespace sns
