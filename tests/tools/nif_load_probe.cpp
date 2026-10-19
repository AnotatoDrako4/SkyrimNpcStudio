// Loads ONE NIF the way the studio does and reports what happened, in a process of its own (audit F-10:
// "measure the test process, not the whole application"). Usage:
//   sns_nif_load_probe <mesh|skeleton|facegen> <file>
//   sns_nif_load_probe scan <folder> <max files>
// Prints one line: mode, file, bytes, outcome (ok / sns::Error / other exception type), message, elapsed ms
// and the process's peak working set. A crash shows up as the process exit code (e.g. 0xC00000FD, stack
// overflow), which the caller records. Inputs are read-only; nothing is written.
// `scan` walks a folder (read-only) and compares, per .nif, sns::nif::prevalidate_nif with a plain nifly Load:
// a file nifly reads that the prevalidation refuses is a FALSE REJECTION, listed by path.
#include <windows.h>
#include <psapi.h>

#include <NifFile.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <typeinfo>

#include "sns/core/error.hpp"
#include "sns/face/facegen.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/nif/safe_load.hpp"

namespace {

int scan(const std::filesystem::path& folder, long long max_files) {
  long long files = 0, both_ok = 0, false_rejections = 0, refused_by_both = 0, only_nifly_fails = 0;
  const auto start = std::chrono::steady_clock::now();
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
       it != end && files < max_files; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || it->path().extension() != ".nif") continue;
    std::ifstream in(it->path(), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    ++files;
    bool pre_ok = true, nifly_ok = true;
    std::string reason;
    try {
      sns::nif::prevalidate_nif(bytes, "scan");
    } catch (const sns::Error& e) {
      pre_ok = false;
      reason = e.what();
    }
    if (bytes.size() <= sns::nif::kMaxNifBytes) {
      try {
        nifly::NifFile nif;
        std::istringstream stream(bytes, std::ios::binary);
        nifly_ok = nif.Load(stream) == 0 && nif.IsValid();
      } catch (...) {
        nifly_ok = false;
      }
    } else {
      nifly_ok = false;
    }
    if (pre_ok && nifly_ok) ++both_ok;
    else if (!pre_ok && nifly_ok) {
      ++false_rejections;
      std::printf("FALSE REJECTION %s | %s\n", it->path().string().c_str(), reason.c_str());
    } else if (!pre_ok) ++refused_by_both;
    else ++only_nifly_fails;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  std::printf("scan | %s | %lld file(s) | both read %lld | false rejections %lld | refused by both %lld | nifly fails, prevalidation passes %lld | %lld ms\n",
              folder.string().c_str(), files, both_ok, false_rejections, refused_by_both, only_nifly_fails, static_cast<long long>(ms));
  return false_rejections == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "scan") return scan(argv[2], std::stoll(argv[3]));
  if (argc != 3) {
    std::fprintf(stderr, "usage: sns_nif_load_probe <mesh|skeleton|facegen> <file>\n");
    return 2;
  }
  const std::string mode = argv[1];
  std::ifstream in(argv[2], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", argv[2]);
    return 2;
  }
  const std::string bytes{std::istreambuf_iterator<char>(in), {}};
  const auto start = std::chrono::steady_clock::now();
  std::string outcome = "ok", message;
  try {
    if (mode == "mesh") {
      const auto mesh = sns::nif::load_mesh(bytes, argv[2]);
      message = std::to_string(mesh.shapes.size()) + " shape(s)";
    } else if (mode == "skeleton") {
      (void)sns::nif::load_skeleton(bytes, argv[2]);
    } else if (mode == "facegen") {
      const auto info = sns::face::inspect_facegen(bytes);
      outcome = info.readable ? "ok" : "unreadable";
    } else {
      std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
      return 2;
    }
  } catch (const sns::Error& e) {
    outcome = "sns::Error";
    message = e.what();
  } catch (const std::exception& e) {
    outcome = typeid(e).name();
    message = e.what();
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  PROCESS_MEMORY_COUNTERS pmc{};
  GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
  std::printf("%s | %s | %zu bytes | %s | %s | %lld ms | peak %.1f MB\n", mode.c_str(), argv[2], bytes.size(), outcome.c_str(),
              message.c_str(), static_cast<long long>(ms), static_cast<double>(pmc.PeakWorkingSetSize) / 1048576.0);
  return 0;
}
