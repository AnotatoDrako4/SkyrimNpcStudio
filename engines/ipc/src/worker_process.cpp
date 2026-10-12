#include "sns/ipc/worker_process.hpp"

#include <windows.h>

#include <random>
#include <sstream>
#include <system_error>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/json_rpc_client.hpp"
#include "sns/ipc/transport.hpp"

namespace sns::ipc {
namespace {

std::string win_error(DWORD code) { return std::system_category().message(static_cast<int>(code)); }

std::wstring quote(const std::wstring& arg) {
  // Arguments we build never contain quotes; paths may contain spaces.
  return L"\"" + arg + L"\"";
}

}  // namespace

struct WorkerProcess::Impl {
  Options options;
  HANDLE job = nullptr;
  HANDLE process = nullptr;
  std::uint32_t pid = 0;

  ~Impl() {
    if (process) CloseHandle(process);
    if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE terminates the worker if still alive
  }
};

std::string WorkerProcess::make_pipe_name() {
  std::random_device rd;
  std::ostringstream name;
  name << "sns-worker-" << GetCurrentProcessId() << "-" << std::hex << rd() << rd();
  return name.str();
}

WorkerProcess::WorkerProcess(Options options) : impl_(std::make_unique<Impl>()) {
  if (options.pipe_name.empty()) options.pipe_name = make_pipe_name();
  impl_->options = std::move(options);
}

WorkerProcess::~WorkerProcess() = default;

void WorkerProcess::start() {
  if (impl_->process) throw Error(ErrorKind::InvalidArgument, "worker already started");
  if (!std::filesystem::exists(impl_->options.executable)) {
    throw Error(ErrorKind::NotFound, "worker executable not found: " + text::path_to_utf8(impl_->options.executable));
  }

  impl_->job = CreateJobObjectW(nullptr, nullptr);
  if (!impl_->job) throw Error(ErrorKind::Io, "CreateJobObject failed: " + win_error(GetLastError()));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  std::wstring command = quote(impl_->options.executable.native()) + L" --pipe " +
                         text::to_wide(impl_->options.pipe_name) + L" --parent-pid " +
                         std::to_wstring(GetCurrentProcessId());

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  HANDLE log = INVALID_HANDLE_VALUE;
  BOOL inherit = FALSE;
  if (impl_->options.log_file) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    log = CreateFileW(impl_->options.log_file->c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) throw Error(ErrorKind::Io, "cannot open worker log: " + win_error(GetLastError()));
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    inherit = TRUE;
  }

  PROCESS_INFORMATION pi{};
  const auto workdir = impl_->options.executable.parent_path();
  const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, inherit,
                                 CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, workdir.c_str(), &si, &pi);
  const DWORD err = GetLastError();
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  if (!ok) throw Error(ErrorKind::Io, "cannot start worker: " + win_error(err));

  if (!AssignProcessToJobObject(impl_->job, pi.hProcess)) {
    const DWORD job_err = GetLastError();
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    throw Error(ErrorKind::Io, "cannot assign worker to job: " + win_error(job_err));
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  impl_->process = pi.hProcess;
  impl_->pid = pi.dwProcessId;
}

std::uint32_t WorkerProcess::pid() const noexcept { return impl_->pid; }

const std::string& WorkerProcess::pipe_name() const noexcept { return impl_->options.pipe_name; }

bool WorkerProcess::running() const {
  return impl_->process && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
}

std::optional<std::uint32_t> WorkerProcess::wait(std::chrono::milliseconds timeout) {
  if (!impl_->process) return std::nullopt;
  if (WaitForSingleObject(impl_->process, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) return std::nullopt;
  DWORD code = 0;
  GetExitCodeProcess(impl_->process, &code);
  return code;
}

void WorkerProcess::terminate() {
  if (impl_->process && running()) {
    TerminateProcess(impl_->process, 1);
    WaitForSingleObject(impl_->process, 5000);
  }
}

std::unique_ptr<JsonRpcClient> connect_worker(const WorkerProcess& worker, std::chrono::milliseconds timeout,
                                              HandshakeResult* handshake_out) {
  auto transport = NamedPipeTransport::connect(worker.pipe_name(), timeout, worker.pid());
  auto client = std::make_unique<JsonRpcClient>(std::move(transport));
  client->start();
  auto result = handshake(*client, "SkyrimNpcStudio", "0.1.0", timeout);
  if (handshake_out) *handshake_out = result;
  return client;
}

}  // namespace sns::ipc
