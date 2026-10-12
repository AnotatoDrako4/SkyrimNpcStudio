#include "sns/mo2/load_order.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace {

using Severity = Diagnostic::Severity;

constexpr std::size_t kMaxFullPlugins = 254;
constexpr std::size_t kMaxLightPlugins = 4096;

bool is_plugin_name(std::string_view name) {
  return text::ends_with_icase(name, ".esp") || text::ends_with_icase(name, ".esm") || text::ends_with_icase(name, ".esl");
}

}  // namespace

EffectiveLoadOrder resolve_load_order(const Profile& profile, const VirtualFileSystem& vfs, const GameInstall& game) {
  EffectiveLoadOrder lo;
  auto diag = [&](Severity s, std::string code, std::string message, std::string subject) {
    lo.diagnostics.push_back({s, std::move(code), std::move(message), std::move(subject)});
  };

  std::unordered_set<std::string> seen;
  auto resolve = [&](const std::string& name, bool active, bool implicit) {
    ResolvedPlugin p;
    p.name = name;
    p.active = active;
    p.implicit = implicit;
    if (auto file = vfs.resolve(name)) {
      p.path = file->absolute;
      p.provider = vfs.providers()[file->provider].name;
      try {
        p.header = read_plugin_header(file->absolute);
      } catch (const Error& e) {
        diag(Severity::Error, "PluginHeaderUnreadable", e.what(), name);
      }
    } else if (active) {
      diag(Severity::Error, "PluginMissing", "Enabled in plugins.txt but no mod provides the file.", name);
    }
    return p;
  };

  // 1-2. Implicit plugins: official masters, then Creation Club content listed in Skyrim.ccc.
  std::vector<std::pair<std::string, bool>> requested;  // name, implicit
  for (const auto& name : implicit_masters()) {
    if (vfs.resolve(name)) requested.emplace_back(name, true);
  }
  for (const auto& name : creation_club_plugins(game)) {
    if (vfs.resolve(name)) requested.emplace_back(name, true);
  }
  // 3. plugins.txt order for the rest.
  for (const auto& entry : profile.plugins) {
    if (entry.active) requested.emplace_back(entry.name, false);
  }

  for (const auto& [name, implicit] : requested) {
    if (!seen.insert(text::to_lower_ascii(name)).second) continue;
    lo.active.push_back(resolve(name, true, implicit));
  }
  for (const auto& entry : profile.plugins) {
    if (entry.active || !seen.insert(text::to_lower_ascii(entry.name)).second) continue;
    lo.inactive.push_back(resolve(entry.name, false, false));
  }
  for (const auto& name : vfs.root_plugins()) {
    if (!seen.insert(text::to_lower_ascii(name)).second) continue;
    lo.inactive.push_back(resolve(name, false, false));
    diag(Severity::Info, "UnlistedPlugin", "Present in Data but not listed in plugins.txt (inactive).", name);
  }

  // 5. The engine always loads masters (ESM flag or .esm/.esl) before regular plugins.
  const auto before = lo.active;
  std::stable_partition(lo.active.begin(), lo.active.end(),
                        [](const ResolvedPlugin& p) { return p.header && p.header->is_master; });
  for (std::size_t i = 0; i < lo.active.size(); ++i) {
    if (lo.active[i].name != before[i].name && lo.active[i].header && lo.active[i].header->is_master) {
      diag(Severity::Info, "MasterHoisted", "Master-flagged plugin listed after regular plugins; the game loads it earlier.",
           lo.active[i].name);
    }
  }

  // 6. Masters must be active and load earlier.
  std::unordered_map<std::string, std::size_t> position;
  for (std::size_t i = 0; i < lo.active.size(); ++i) position[text::to_lower_ascii(lo.active[i].name)] = i;
  std::size_t full = 0, light = 0;
  for (std::size_t i = 0; i < lo.active.size(); ++i) {
    const auto& p = lo.active[i];
    if (!p.header) continue;
    (p.header->is_light ? light : full)++;
    for (const auto& master : p.header->masters) {
      const auto it = position.find(text::to_lower_ascii(master));
      if (it == position.end()) {
        diag(Severity::Error, "MissingMaster", "Requires '" + master + "', which is not active.", p.name);
      } else if (it->second > i) {
        diag(Severity::Error, "MasterAfterDependent", "Requires '" + master + "', which loads after it.", p.name);
      }
    }
  }
  if (full > kMaxFullPlugins) diag(Severity::Error, "TooManyPlugins", std::to_string(full) + " full plugins exceed the 254 limit.", {});
  if (light > kMaxLightPlugins) diag(Severity::Error, "TooManyLightPlugins", std::to_string(light) + " light plugins exceed the 4096 limit.", {});

  // 7. loadorder.txt is MO2's own record; report when it disagrees with plugins.txt.
  if (!profile.load_order.empty()) {
    std::vector<std::string> recorded;
    for (const auto& name : profile.load_order) {
      if (position.contains(text::to_lower_ascii(name)) && is_plugin_name(name)) recorded.push_back(text::to_lower_ascii(name));
    }
    std::vector<std::string> computed;
    for (const auto& p : lo.active) computed.push_back(text::to_lower_ascii(p.name));
    std::vector<std::string> computed_recorded;
    std::copy_if(computed.begin(), computed.end(), std::back_inserter(computed_recorded), [&](const std::string& n) {
      return std::find(recorded.begin(), recorded.end(), n) != recorded.end();
    });
    if (computed_recorded != recorded) {
      diag(Severity::Warning, "LoadOrderTxtMismatch",
           "loadorder.txt and plugins.txt disagree on the order of active plugins; plugins.txt is used.", {});
    }
  }
  return lo;
}

}  // namespace sns::mo2
