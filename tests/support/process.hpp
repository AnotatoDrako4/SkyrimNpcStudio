#pragma once

#include <windows.h>

#include <stdexcept>
#include <string>

namespace sns::test {

// Runs a command line to completion and returns its exit code.
inline DWORD run_and_wait(std::wstring command_line, DWORD timeout_ms = 120'000) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    throw std::runtime_error("CreateProcess failed");
  }
  CloseHandle(pi.hThread);
  const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  DWORD code = 1;
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hProcess);
  return code;
}

}  // namespace sns::test
