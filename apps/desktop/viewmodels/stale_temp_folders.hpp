#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sns::app {

#ifdef _WIN32
// True when a process with this id is running (or exists but belongs to another user).
inline bool process_alive(qint64 pid) {
  const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
}
#endif

// Removes "<prefix><pid>[-anything]" folders of the system temp directory left by studios that ended without cleaning up
// (crash, kill). The folder of a running studio, this one included, is never touched. Returns how many were removed.
inline int remove_stale_temp_folders(const QString& prefix) {
  int removed = 0;
#ifdef _WIN32
  const QDir temp = QDir::temp();
  for (const auto& name : temp.entryList({prefix + QStringLiteral("*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok = false;
    const auto pid = name.mid(prefix.size()).section('-', 0, 0).toLongLong(&ok);
    if (ok && pid != QCoreApplication::applicationPid() && !process_alive(pid) && QDir(temp.filePath(name)).removeRecursively())
      ++removed;
  }
#else
  Q_UNUSED(prefix);
#endif
  return removed;
}

}  // namespace sns::app
