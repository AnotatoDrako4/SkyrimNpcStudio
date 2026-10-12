#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace sns::ipc {

class JsonRpcClient;
struct HandshakeResult;

// Launches the .NET Bethesda worker inside a Job Object configured with KILL_ON_JOB_CLOSE, so the
// worker can never outlive the studio, even if the studio crashes.
class WorkerProcess {
 public:
  struct Options {
    std::filesystem::path executable;
    // Empty: a unique name is generated (see make_pipe_name()).
    std::string pipe_name;
    // When set, the worker's stdout/stderr are appended to this file instead of the console.
    std::optional<std::filesystem::path> log_file;
  };

  // "sns-worker-<pid>-<random>", unpredictable to other processes.
  static std::string make_pipe_name();

  explicit WorkerProcess(Options options);
  ~WorkerProcess();
  WorkerProcess(const WorkerProcess&) = delete;
  WorkerProcess& operator=(const WorkerProcess&) = delete;

  void start();
  [[nodiscard]] std::uint32_t pid() const noexcept;
  [[nodiscard]] const std::string& pipe_name() const noexcept;
  [[nodiscard]] bool running() const;

  // Returns the exit code if the process ended within the timeout.
  std::optional<std::uint32_t> wait(std::chrono::milliseconds timeout);
  void terminate();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Connects to a started worker (verifying the pipe owner pid) and performs the protocol handshake.
std::unique_ptr<JsonRpcClient> connect_worker(const WorkerProcess& worker, std::chrono::milliseconds timeout,
                                              HandshakeResult* handshake_out = nullptr);

}  // namespace sns::ipc
