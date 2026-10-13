#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Crash diagnostics for the studio process.
//
//  - A session log (diagnostics/sessions/session-<time>-<pid>.log) receives every breadcrumb line
//    as it happens, flushed, so it survives even failures no handler can see (heap corruption and
//    other fail-fast terminations go straight to Windows Error Reporting).
//  - The last breadcrumbs, the context (NPC, plugin...) and the operations in progress on each
//    thread are also kept in fixed-size memory that the crash handler writes without allocating.
//  - On an unhandled native exception (access violation...), std::terminate, abort(), a pure
//    virtual call or an invalid CRT parameter, a report (.txt: exception, module+offset, thread,
//    context, active operations, last breadcrumbs, symbolized stack) and a minidump (.dmp) are
//    written to diagnostics/crashes. The handler then lets the process die: it never resumes
//    execution on memory that may be corrupt.
//  - scripts/enable-crash-dumps.ps1 (administrator) registers WER LocalDumps for the cases above
//    that bypass in-process handlers.
namespace sns::app::diag {

// Creates the folders, opens the session log, installs the handlers. Returns the log of the
// previous session when it did not end cleanly (empty path otherwise).
std::filesystem::path install(const std::filesystem::path& directory);
// Marks the session log as cleanly closed (called when the event loop returns).
void shutdown();
[[nodiscard]] std::filesystem::path directory();

// Values repeated in every crash report ("npc", "winningPlugin", "operation"...).
void set_context(std::string_view key, std::string_view value);

// One breadcrumb: time, thread id and `category: message`, to the session log and the ring.
void record(std::string_view category, std::string_view message);

// Unique id for a background task (FaceGen build, scene build...), shown in breadcrumbs.
std::uint64_t next_task_id();

// An operation running on the current thread. Its name, task id and last stage appear in the
// crash report while it is alive; begin/end/failure are breadcrumbs.
class Operation {
 public:
  Operation(std::string_view name, std::uint64_t task_id, std::string_view details = {});
  ~Operation();
  Operation(const Operation&) = delete;
  Operation& operator=(const Operation&) = delete;

  void stage(std::string_view stage);
  void fail(std::string_view what);
  void succeed() { done_ = true; }

 private:
  int slot_ = -1;
  std::uint64_t task_id_ = 0;
  bool done_ = false;
  bool failed_ = false;
};

// For tests: writes a report for the current context without crashing.
std::filesystem::path write_report_now(std::string_view reason);

}  // namespace sns::app::diag
