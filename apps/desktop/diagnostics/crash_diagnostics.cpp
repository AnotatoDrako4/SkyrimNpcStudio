#include "crash_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <dbghelp.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
// clang-format on
#pragma comment(lib, "dbghelp.lib")
#endif

namespace sns::app::diag {
namespace {

// Everything the crash handler reads lives in fixed arrays: writing the report must not allocate
// (the heap may be the thing that is broken).
constexpr int kRingLines = 256;
constexpr int kLineChars = 384;
constexpr int kSlots = 32;
constexpr int kContextEntries = 16;
constexpr const char* kCleanEnd = "=== session end (clean) ===";

struct RingLine {
  std::atomic<std::uint64_t> seq{0};
  char text[kLineChars]{};
};
struct OperationSlot {
  std::atomic<bool> used{false};
  unsigned long thread = 0;
  std::uint64_t task = 0;
  std::int64_t started_ms = 0;
  char name[48]{};
  char stage[128]{};
  char details[200]{};
};
struct ContextEntry {
  char key[32]{};
  char value[256]{};
};

RingLine g_ring[kRingLines];
std::atomic<std::uint64_t> g_ring_seq{0};
OperationSlot g_slots[kSlots];
ContextEntry g_context[kContextEntries];
std::mutex g_context_mutex;
std::atomic<std::uint64_t> g_task{0};
std::atomic<bool> g_crashing{false};

std::mutex g_log_mutex;
std::FILE* g_log = nullptr;
std::filesystem::path g_dir;
std::filesystem::path g_session;
#ifdef _WIN32
wchar_t g_crash_dir[MAX_PATH]{};
wchar_t g_exe_dir[MAX_PATH]{};
#endif
unsigned long g_main_thread = 0;
const auto g_start = std::chrono::steady_clock::now();

void copy_trunc(char* dst, std::size_t cap, std::string_view src) {
  const auto n = std::min(cap - 1, src.size());
  std::memcpy(dst, src.data(), n);
  dst[n] = '\0';
}

unsigned long thread_id() {
#ifdef _WIN32
  return GetCurrentThreadId();
#else
  return 0;
#endif
}

std::int64_t uptime_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_start).count();
}

// "HH:MM:SS.mmm" local time, no allocation.
void clock_text(char (&out)[16]) {
#ifdef _WIN32
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::snprintf(out, sizeof(out), "%02u:%02u:%02u.%03u", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
#else
  std::snprintf(out, sizeof(out), "%lld", static_cast<long long>(uptime_ms()));
#endif
}

void push_ring(const char* line) {
  const auto seq = g_ring_seq.fetch_add(1) + 1;
  auto& slot = g_ring[seq % kRingLines];
  copy_trunc(slot.text, sizeof(slot.text), line);
  slot.seq.store(seq, std::memory_order_release);
}

void write_line(const char* line) {
  push_ring(line);
  if (g_crashing.load()) return;  // the crash handler writes its own file; never block it here
  std::lock_guard lock(g_log_mutex);
  if (!g_log) return;
  std::fputs(line, g_log);
  std::fputc('\n', g_log);
  std::fflush(g_log);
}

#ifdef _WIN32
// ------------------------------------------------------------------------------------------ report
class ReportFile {
 public:
  explicit ReportFile(const wchar_t* path)
      : file_(CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
  ~ReportFile() {
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  }
  bool ok() const { return file_ != INVALID_HANDLE_VALUE; }
  void put(const char* text) {
    if (!ok()) return;
    DWORD written = 0;
    WriteFile(file_, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
  }
  template <typename... Args>
  void printf(const char* format, Args... args) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    put(buffer);
  }

 private:
  HANDLE file_;
};

void describe_module(DWORD64 address, char* out, std::size_t cap) {
  HMODULE module = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module) && module) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    char narrow[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
    std::snprintf(out, cap, "%s+0x%llx", narrow,
                  static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
  } else {
    std::snprintf(out, cap, "<no module>");
  }
}

const char* exception_name(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun / fail-fast";
    case 0xE06D7363: return "unhandled C++ exception";
    default: return "native exception";
  }
}

void write_stack(ReportFile& report, CONTEXT context) {
  const HANDLE process = GetCurrentProcess();
  const HANDLE thread = GetCurrentThread();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
  char search[MAX_PATH * 3]{};
  WideCharToMultiByte(CP_UTF8, 0, g_exe_dir, -1, search, sizeof(search), nullptr, nullptr);
  SymInitialize(process, search, TRUE);
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  alignas(SYMBOL_INFO) char symbol_buffer[sizeof(SYMBOL_INFO) + 512]{};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
  report.put("\nstack of the failing thread:\n");
  for (int i = 0; i < 64; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                     SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    const auto pc = frame.AddrPC.Offset;
    char module[MAX_PATH + 32];
    describe_module(pc, module, sizeof(module));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    const bool named = SymFromAddr(process, pc, &displacement, symbol);
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    const bool lined = SymGetLineFromAddr64(process, pc, &line_displacement, &line);
    report.printf("  #%02d %s  %s%s", i, module, named ? symbol->Name : "?", named ? "" : "");
    if (lined) report.printf("  (%s:%lu)", line.FileName, line.LineNumber);
    report.put("\n");
  }
  SymCleanup(process);
}

std::filesystem::path write_report(EXCEPTION_POINTERS* info, const char* reason) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t base[MAX_PATH + 64];
  swprintf(base, MAX_PATH + 64, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u-%lu", g_crash_dir, t.wYear, t.wMonth, t.wDay, t.wHour,
           t.wMinute, t.wSecond, GetCurrentProcessId());
  wchar_t txt_path[MAX_PATH + 80], dmp_path[MAX_PATH + 80];
  swprintf(txt_path, MAX_PATH + 80, L"%ls.txt", base);
  swprintf(dmp_path, MAX_PATH + 80, L"%ls.dmp", base);

  ReportFile report(txt_path);
  report.put("Skyrim NPC Studio - crash report\n");
  report.printf("reason: %s\n", reason);
  report.printf("process %lu, thread %lu%s, uptime %lld ms\n", GetCurrentProcessId(), thread_id(),
                thread_id() == g_main_thread ? " (main/GUI thread)" : " (background thread)", static_cast<long long>(uptime_ms()));
  if (info && info->ExceptionRecord) {
    const auto* record = info->ExceptionRecord;
    char module[MAX_PATH + 32];
    describe_module(reinterpret_cast<DWORD64>(record->ExceptionAddress), module, sizeof(module));
    report.printf("exception 0x%08lX (%s) at %s\n", record->ExceptionCode, exception_name(record->ExceptionCode), module);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
      const auto op = record->ExceptionInformation[0];
      report.printf("  %s address 0x%llx\n", op == 0 ? "reading" : op == 1 ? "writing" : "executing",
                    static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
  }

  report.put("\ncontext:\n");
  {
    // try_lock: a thread that crashed while holding it must not block the report.
    std::unique_lock lock(g_context_mutex, std::try_to_lock);
    for (const auto& c : g_context) {
      if (c.key[0]) report.printf("  %s = %s\n", c.key, c.value);
    }
  }

  report.put("\noperations in progress:\n");
  bool any = false;
  for (const auto& s : g_slots) {
    if (!s.used.load()) continue;
    any = true;
    report.printf("  thread %lu%s: %s #%llu, stage '%s', running %lld ms%s%s\n", s.thread,
                  s.thread == g_main_thread ? " (main)" : "", s.name, static_cast<unsigned long long>(s.task), s.stage,
                  static_cast<long long>(uptime_ms() - s.started_ms), s.details[0] ? " - " : "", s.details);
  }
  if (!any) report.put("  (none)\n");

  report.put("\nlast breadcrumbs (oldest first):\n");
  const auto last = g_ring_seq.load();
  const auto first = last > kRingLines ? last - kRingLines + 1 : 1;
  for (auto seq = first; seq <= last; ++seq) {
    const auto& line = g_ring[seq % kRingLines];
    if (line.seq.load(std::memory_order_acquire) != seq) continue;
    report.printf("  %s\n", line.text);
  }

  // The dump first: if symbolization below fails, the dump still has everything.
  bool dumped = false;
  if (const HANDLE dump = CreateFileW(dmp_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData);
    dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type, info ? &exception : nullptr, nullptr,
                               nullptr) != FALSE;
    CloseHandle(dump);
  }
  char narrow[MAX_PATH * 3]{};
  WideCharToMultiByte(CP_UTF8, 0, dmp_path, -1, narrow, sizeof(narrow), nullptr, nullptr);
  report.printf("\nminidump: %s%s\n", narrow, dumped ? "" : " (could not be written)");

  CONTEXT context{};
  if (info && info->ContextRecord) {
    context = *info->ContextRecord;
  } else {
    RtlCaptureContext(&context);
  }
  write_stack(report, context);
  return std::filesystem::path(txt_path);
}

void report_and_continue_dying(EXCEPTION_POINTERS* info, const char* reason) {
  if (g_crashing.exchange(true)) return;
  char line[kLineChars];
  std::snprintf(line, sizeof(line), "FATAL %s - writing crash report", reason);
  push_ring(line);
  const auto path = write_report(info, reason);
  // Last line of the session log points at the report (best effort, never blocking).
  if (std::unique_lock lock(g_log_mutex, std::try_to_lock); lock && g_log) {
    std::fprintf(g_log, "FATAL %s - report %s\n", reason, path.string().c_str());
    std::fflush(g_log);
  }
  std::fprintf(stderr, "FATAL %s - crash report: %s\n", reason, path.string().c_str());
}

LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;

LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* info) {
  report_and_continue_dying(info, "unhandled native exception");
  // Then the previous filter (the C runtime's turns an escaped C++ exception into terminate), and
  // finally Windows Error Reporting ends the process. Execution never resumes here.
  if (g_previous_filter) return g_previous_filter(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

void on_terminate() {
  char reason[kLineChars] = "std::terminate";
  if (const auto current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception& e) {
      std::snprintf(reason, sizeof(reason), "std::terminate: uncaught exception: %s", e.what());
    } catch (...) {
      std::snprintf(reason, sizeof(reason), "std::terminate: uncaught non-standard exception");
    }
  }
  report_and_continue_dying(nullptr, reason);
  std::abort();
}

void on_abort(int) { report_and_continue_dying(nullptr, "abort()"); }
void on_purecall() {
  report_and_continue_dying(nullptr, "pure virtual function call");
  std::abort();
}
void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
  report_and_continue_dying(nullptr, "invalid parameter passed to a C runtime function");
  std::abort();
}
#ifdef _DEBUG
// Debug CRT reports ("HEAP CORRUPTION DETECTED", failed _ASSERTs): instead of a modal message box
// (which blocks unattended runs and keeps the corrupted process alive), the message goes to the
// session log and errors end the process with the usual crash report and minidump.
int on_crt_report(int type, char* message, int* return_value) {
  if (return_value) *return_value = 0;
  if (type == _CRT_WARN) return TRUE;  // handled: no dialog, keep running
  // The heap may be damaged and the CRT can report again while the report is written (it
  // allocates): only the first report counts, nothing after it touches the C runtime.
  static std::atomic<bool> reporting{false};
  if (reporting.exchange(true)) return TRUE;
  report_and_continue_dying(nullptr, message ? message : "debug CRT report");
  TerminateProcess(GetCurrentProcess(), 3);
  return TRUE;
}
#endif
#endif

bool ended_cleanly(const std::filesystem::path& log) {
  std::FILE* f = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&f, log.c_str(), L"rb") != 0) f = nullptr;
#else
  f = std::fopen(log.c_str(), "rb");
#endif
  if (!f) return true;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  const long tail = std::min<long>(size, 4096);
  std::fseek(f, size - tail, SEEK_SET);
  std::string text(static_cast<std::size_t>(tail), '\0');
  const auto read = std::fread(text.data(), 1, text.size(), f);
  std::fclose(f);
  text.resize(read);
  return text.find(kCleanEnd) != std::string::npos;
}

void prune(const std::filesystem::path& dir, std::string_view prefix, std::size_t keep) {
  std::error_code ec;
  std::vector<std::filesystem::directory_entry> files;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.is_regular_file(ec) && e.path().filename().string().starts_with(prefix)) files.push_back(e);
  }
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path().filename() > b.path().filename(); });
  for (std::size_t i = keep; i < files.size(); ++i) std::filesystem::remove(files[i].path(), ec);
}

}  // namespace

std::filesystem::path install(const std::filesystem::path& directory) {
  g_dir = directory;
  g_main_thread = thread_id();
  std::error_code ec;
  const auto sessions = directory / "sessions";
  const auto crashes = directory / "crashes";
  std::filesystem::create_directories(sessions, ec);
  std::filesystem::create_directories(crashes, ec);

  // The newest previous session log (names sort by time).
  std::filesystem::path previous;
  for (const auto& e : std::filesystem::directory_iterator(sessions, ec)) {
    if (e.path().extension() == ".log" && (previous.empty() || e.path().filename() > previous.filename())) previous = e.path();
  }
  prune(sessions, "session-", 20);
  prune(crashes, "crash-", 40);  // .txt + .dmp pairs: 20 crashes

#ifdef _WIN32
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[96];
  swprintf(name, 96, L"session-%04u%02u%02u-%02u%02u%02u-%lu.log", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
           GetCurrentProcessId());
  g_session = sessions / name;
  {
    std::lock_guard lock(g_log_mutex);
    g_log = _wfsopen(g_session.c_str(), L"ab", _SH_DENYWR);
  }
  wcsncpy_s(g_crash_dir, crashes.c_str(), _TRUNCATE);
  GetModuleFileNameW(nullptr, g_exe_dir, MAX_PATH);
  if (auto* slash = wcsrchr(g_exe_dir, L'\\')) *slash = L'\0';

  g_previous_filter = SetUnhandledExceptionFilter(&on_unhandled_exception);
  std::set_terminate(&on_terminate);
  std::signal(SIGABRT, &on_abort);
  _set_purecall_handler(&on_purecall);
  _set_invalid_parameter_handler(&on_invalid_parameter);
#ifdef _DEBUG
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &on_crt_report);
#endif
#endif
  char line[kLineChars];
  std::snprintf(line, sizeof(line), "=== session start, pid %lu, main thread %lu ===",
#ifdef _WIN32
                GetCurrentProcessId(),
#else
                0ul,
#endif
                g_main_thread);
  write_line(line);
  return !previous.empty() && !ended_cleanly(previous) ? previous : std::filesystem::path{};
}

void shutdown() {
  write_line(kCleanEnd);
  std::lock_guard lock(g_log_mutex);
  if (g_log) {
    std::fclose(g_log);
    g_log = nullptr;
  }
}

std::filesystem::path directory() { return g_dir; }

void set_context(std::string_view key, std::string_view value) {
  std::lock_guard lock(g_context_mutex);
  ContextEntry* slot = nullptr;
  for (auto& c : g_context) {
    if (std::string_view(c.key) == key) {
      slot = &c;
      break;
    }
    if (!slot && !c.key[0]) slot = &c;
  }
  if (!slot) return;
  copy_trunc(slot->key, sizeof(slot->key), key);
  copy_trunc(slot->value, sizeof(slot->value), value);
}

void record(std::string_view category, std::string_view message) {
  char clock[16];
  clock_text(clock);
  char line[kLineChars];
  std::snprintf(line, sizeof(line), "%s [%5lu%s] %.*s: %.*s", clock, thread_id(), thread_id() == g_main_thread ? "*" : " ",
                static_cast<int>(category.size()), category.data(), static_cast<int>(message.size()), message.data());
  write_line(line);
}

std::uint64_t next_task_id() { return g_task.fetch_add(1) + 1; }

Operation::Operation(std::string_view name, std::uint64_t task_id, std::string_view details) : task_id_(task_id) {
  for (int i = 0; i < kSlots; ++i) {
    bool expected = false;
    if (g_slots[i].used.compare_exchange_strong(expected, true)) {
      slot_ = i;
      break;
    }
  }
  if (slot_ >= 0) {
    auto& s = g_slots[slot_];
    s.thread = thread_id();
    s.task = task_id;
    s.started_ms = uptime_ms();
    copy_trunc(s.name, sizeof(s.name), name);
    copy_trunc(s.stage, sizeof(s.stage), "start");
    copy_trunc(s.details, sizeof(s.details), details);
  }
  char text[kLineChars];
  std::snprintf(text, sizeof(text), "%.*s #%llu begin%s%.*s", static_cast<int>(name.size()), name.data(),
                static_cast<unsigned long long>(task_id), details.empty() ? "" : " - ", static_cast<int>(details.size()),
                details.data());
  record("op", text);
}

Operation::~Operation() {
  const char* name = slot_ >= 0 ? g_slots[slot_].name : "operation";
  char text[kLineChars];
  if (failed_) {
    std::snprintf(text, sizeof(text), "%s #%llu end (failed)", name, static_cast<unsigned long long>(task_id_));
  } else if (!done_ && std::uncaught_exceptions() > 0) {
    std::snprintf(text, sizeof(text), "%s #%llu abandoned by an exception at stage '%s'", name,
                  static_cast<unsigned long long>(task_id_), slot_ >= 0 ? g_slots[slot_].stage : "?");
  } else {
    std::snprintf(text, sizeof(text), "%s #%llu end%s (%lld ms)", name, static_cast<unsigned long long>(task_id_),
                  done_ ? "" : " (superseded or cancelled)",
                  static_cast<long long>(slot_ >= 0 ? uptime_ms() - g_slots[slot_].started_ms : 0));
  }
  record("op", text);
  if (slot_ >= 0) g_slots[slot_].used.store(false);
}

void Operation::stage(std::string_view stage) {
  if (slot_ >= 0) copy_trunc(g_slots[slot_].stage, sizeof(g_slots[slot_].stage), stage);
  char text[kLineChars];
  std::snprintf(text, sizeof(text), "%s #%llu stage %.*s", slot_ >= 0 ? g_slots[slot_].name : "operation",
                static_cast<unsigned long long>(task_id_), static_cast<int>(stage.size()), stage.data());
  record("op", text);
}

void Operation::fail(std::string_view what) {
  failed_ = true;
  char text[kLineChars];
  std::snprintf(text, sizeof(text), "%s #%llu FAILED at stage '%s': %.*s", slot_ >= 0 ? g_slots[slot_].name : "operation",
                static_cast<unsigned long long>(task_id_), slot_ >= 0 ? g_slots[slot_].stage : "?",
                static_cast<int>(what.size()), what.data());
  record("op", text);
}

std::filesystem::path write_report_now(std::string_view reason) {
#ifdef _WIN32
  char text[kLineChars];
  copy_trunc(text, sizeof(text), reason);
  return write_report(nullptr, text);
#else
  (void)reason;
  return {};
#endif
}

}  // namespace sns::app::diag
