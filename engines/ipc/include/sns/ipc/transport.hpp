#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sns::ipc {

// A duplex byte stream. read() and write() may be called concurrently from different threads.
class Transport {
 public:
  virtual ~Transport() = default;

  // Blocks until data arrives. Returns 0 at end of stream or after close(). Throws on I/O errors.
  virtual std::size_t read(char* buffer, std::size_t capacity) = 0;

  // Writes all bytes or throws.
  virtual void write(std::string_view bytes) = 0;

  // Unblocks a pending read() and makes later writes fail. Safe to call more than once and from
  // any thread; does not release OS resources while a read may still be running.
  virtual void close() = 0;

  // Releases the underlying OS handle so the peer sees end-of-stream. Call only once no thread is
  // inside read() any more (JsonRpcClient does it after joining its reader thread).
  virtual void release() { close(); }
};

// Client end of a Windows named pipe (\\.\pipe\<name>) using overlapped I/O, so that a blocked
// read on the reader thread never serializes writes issued from other threads.
class NamedPipeTransport final : public Transport {
 public:
  // Waits up to `timeout` for the server to create the pipe. When `expected_server_pid` is set the
  // connection is rejected unless that process owns the pipe (protects against pipe squatting).
  static std::unique_ptr<NamedPipeTransport> connect(std::string_view pipe_name, std::chrono::milliseconds timeout,
                                                     std::optional<std::uint32_t> expected_server_pid = std::nullopt);

  ~NamedPipeTransport() override;
  NamedPipeTransport(const NamedPipeTransport&) = delete;
  NamedPipeTransport& operator=(const NamedPipeTransport&) = delete;

  std::size_t read(char* buffer, std::size_t capacity) override;
  void write(std::string_view bytes) override;
  void close() override;
  void release() override;

 private:
  struct Impl;
  explicit NamedPipeTransport(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace sns::ipc
