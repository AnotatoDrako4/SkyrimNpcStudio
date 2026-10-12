#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <system_error>
#include <thread>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/transport.hpp"

namespace sns::ipc {
namespace {

std::string win_error(DWORD code) { return std::system_category().message(static_cast<int>(code)); }

class Event {
 public:
  Event() : h_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!h_) throw Error(ErrorKind::Io, "CreateEvent failed: " + win_error(GetLastError()));
  }
  ~Event() { CloseHandle(h_); }
  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;
  [[nodiscard]] HANDLE get() const noexcept { return h_; }

 private:
  HANDLE h_;
};

constexpr DWORD kWriteTimeoutMs = 30'000;

}  // namespace

struct NamedPipeTransport::Impl {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  Event closed;       // signalled by close()
  Event read_done;
  Event write_done;
  std::mutex write_mutex;
  std::atomic<bool> is_closed{false};

  ~Impl() {
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
  }
};

NamedPipeTransport::NamedPipeTransport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

NamedPipeTransport::~NamedPipeTransport() { release(); }

std::unique_ptr<NamedPipeTransport> NamedPipeTransport::connect(std::string_view pipe_name,
                                                                std::chrono::milliseconds timeout,
                                                                std::optional<std::uint32_t> expected_server_pid) {
  const std::wstring full = L"\\\\.\\pipe\\" + text::to_wide(pipe_name);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  auto impl = std::make_unique<Impl>();

  for (;;) {
    impl->pipe = CreateFileW(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (impl->pipe != INVALID_HANDLE_VALUE) break;

    const DWORD err = GetLastError();
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      throw Error(ErrorKind::Timeout, "worker pipe '" + std::string(pipe_name) + "' not available: " + win_error(err));
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    if (err == ERROR_PIPE_BUSY) {
      WaitNamedPipeW(full.c_str(), static_cast<DWORD>(std::min<long long>(remaining.count(), 1000)));
    } else if (err == ERROR_FILE_NOT_FOUND) {
      // The server has not created the pipe yet (worker still starting).
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } else {
      throw Error(ErrorKind::Io, "cannot open worker pipe: " + win_error(err));
    }
  }

  if (expected_server_pid) {
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(impl->pipe, &server_pid) || server_pid != *expected_server_pid) {
      throw Error(ErrorKind::Protocol, "worker pipe is owned by an unexpected process (pid " +
                                           std::to_string(server_pid) + "); refusing to connect");
    }
  }

  return std::unique_ptr<NamedPipeTransport>(new NamedPipeTransport(std::move(impl)));
}

std::size_t NamedPipeTransport::read(char* buffer, std::size_t capacity) {
  if (impl_->is_closed) return 0;
  OVERLAPPED ov{};
  ov.hEvent = impl_->read_done.get();
  ResetEvent(ov.hEvent);

  DWORD got = 0;
  const auto request = static_cast<DWORD>(std::min<std::size_t>(capacity, 1u << 20));
  if (ReadFile(impl_->pipe, buffer, request, &got, &ov)) return got;

  DWORD err = GetLastError();
  if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) return 0;
  if (err != ERROR_IO_PENDING) throw Error(ErrorKind::Io, "pipe read failed: " + win_error(err));

  HANDLE waits[] = {ov.hEvent, impl_->closed.get()};
  const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
  if (which != WAIT_OBJECT_0) {
    CancelIoEx(impl_->pipe, &ov);
    GetOverlappedResult(impl_->pipe, &ov, &got, TRUE);  // the OVERLAPPED must outlive the I/O
    return 0;
  }
  if (!GetOverlappedResult(impl_->pipe, &ov, &got, FALSE)) {
    err = GetLastError();
    if (err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED || err == ERROR_PIPE_NOT_CONNECTED) return 0;
    throw Error(ErrorKind::Io, "pipe read failed: " + win_error(err));
  }
  return got;
}

void NamedPipeTransport::write(std::string_view bytes) {
  std::lock_guard lock(impl_->write_mutex);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (impl_->is_closed) throw Error(ErrorKind::Io, "pipe is closed");
    OVERLAPPED ov{};
    ov.hEvent = impl_->write_done.get();
    ResetEvent(ov.hEvent);
    DWORD written = 0;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
    if (!WriteFile(impl_->pipe, bytes.data() + offset, chunk, &written, &ov)) {
      const DWORD err = GetLastError();
      if (err != ERROR_IO_PENDING) throw Error(ErrorKind::Io, "pipe write failed: " + win_error(err));
      HANDLE waits[] = {ov.hEvent, impl_->closed.get()};
      const DWORD which = WaitForMultipleObjects(2, waits, FALSE, kWriteTimeoutMs);
      if (which != WAIT_OBJECT_0) {
        CancelIoEx(impl_->pipe, &ov);
        GetOverlappedResult(impl_->pipe, &ov, &written, TRUE);
        throw Error(which == WAIT_TIMEOUT ? ErrorKind::Timeout : ErrorKind::Io, "pipe write did not complete");
      }
      if (!GetOverlappedResult(impl_->pipe, &ov, &written, FALSE)) {
        throw Error(ErrorKind::Io, "pipe write failed: " + win_error(GetLastError()));
      }
    }
    offset += written;
  }
}

void NamedPipeTransport::close() {
  if (!impl_ || impl_->is_closed.exchange(true)) return;
  SetEvent(impl_->closed.get());
  CancelIoEx(impl_->pipe, nullptr);
}

void NamedPipeTransport::release() {
  if (!impl_) return;
  close();
  std::lock_guard lock(impl_->write_mutex);  // no write can be using the handle
  if (impl_->pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->pipe);
    impl_->pipe = INVALID_HANDLE_VALUE;
  }
}

}  // namespace sns::ipc
