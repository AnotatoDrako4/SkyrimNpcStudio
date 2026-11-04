// sns-probe BodySlide commands: reproduce real BodySlide builds and RaceMenu morphs from the files of a profile, read-only.
//
//   sns-probe bodyslide-build --roots "<mod1>;<mod2>;..." --osp <Data path> --set <name> --big 0|1 --compare <file.nif>
//                             [--zeroed | --preset-file <file.xml> --preset <name>]
//       BodySlideApp::ApplySliders on the project's source NIF + OSD/BSD data, then a vertex-by-vertex comparison with a mesh
//       BodySlide itself wrote. --roots are folders whose content is Data-relative, highest priority first (MO2 order).
//   sns-probe bodyslide-match  (same as build, but --presets-list <file of preset .xml paths> instead of one preset)
//       Which preset (or zeroed sliders) reproduces a built mesh: the residual of every preset, best first.
//   sns-probe tri-apply --nif <file.nif> --tri <file.tri> --preset-file <file.xml> --preset <name> --weight <0..100>
//                       [--compare <file.nif>]
//       RaceMenu BodyMorph (as Body Change NG drives it) on a mesh: per-shape report and an optional comparison.
//   sns-probe fit-check --body <file.nif> --body-shape <name> --garment <file.nif> [--garment-shape <name>]
//       body::measure_penetration: garment vertices inside the body in the reference pose.
//   sns-probe bodyslide-write (same inputs as build) --out <folder> [--morphs] [--compare <file.nif> --big 0|1]
//       body::build_slider_set: writes <folder>\<OutputPath>\<OutputFile>_0/_1.nif as BodySlide would; with --morphs also
//       <OutputFile>.tri and the BODYTRI string («Build Morphs»); with --compare the written file is read back and its
//       positions, normals and tangents are compared shape by shape.
//   sns-probe bodyslide-batch --instance <ModOrganizer.ini> [--profile <name>] --preset <name> --out <new folder> [--only <text>] [--include-ambiguous x]
//       Every slider set of the preset's body family in the profile, built with the preset into <out>\mod, with <out>\informe.csv.
// Only bodyslide-write and bodyslide-batch write, and only inside --out.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "sns/body/bodyslide_build.hpp"
#include "sns/body/bodyslide_morphs.hpp"
#include "sns/body/bodyslide_presets.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/game.hpp"
#include "sns/mo2/instance.hpp"
#include "sns/mo2/profile.hpp"
#include "sns/mo2/vfs.hpp"
#include "sns/nif/mesh.hpp"

namespace {

using namespace sns;

struct Args {
  std::map<std::string, std::string> values;
  bool has(const std::string& k) const { return values.count(k) > 0; }
  std::string get(const std::string& k) const {
    const auto it = values.find(k);
    return it == values.end() ? std::string() : it->second;
  }
};

Args parse_args(int argc, char** argv, int first) {
  Args a;
  for (int i = first; i < argc; ++i) {
    std::string k = argv[i];
    if (!k.starts_with("--")) continue;
    if (k == "--zeroed" || k == "--morphs") {
      a.values[k] = "1";
      continue;
    }
    if (i + 1 < argc) a.values[k] = argv[++i];
  }
  return a;
}

std::vector<std::filesystem::path> split_roots(const std::string& s) {
  std::vector<std::filesystem::path> out;
  std::string_view rest = s;
  while (!rest.empty()) {
    const auto semi = rest.find(';');
    const auto part = text::trim(rest.substr(0, semi));
    if (!part.empty()) out.push_back(text::path_from_utf8(std::string(part)));
    rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
  }
  return out;
}

std::optional<std::string> read_from_roots(const std::vector<std::filesystem::path>& roots, const std::string& data_path) {
  for (const auto& root : roots) {
    std::error_code ec;
    const auto p = root / text::path_from_utf8(data_path);
    if (std::filesystem::is_regular_file(p, ec)) return file_io::read_file(p, 1024ull * 1024 * 1024);
  }
  return std::nullopt;
}

std::optional<body::BodySlidePreset> load_preset(const std::string& file, const std::string& name) {
  const auto xml = file_io::read_text_file(text::path_from_utf8(file));
  const auto parsed = body::parse_slider_presets(xml, file, "probe");
  for (const auto& p : parsed.presets) {
    if (p.name == name) return p;
  }
  return std::nullopt;
}

struct Comparison {
  std::string shape;
  std::size_t ours = 0, theirs = 0, zapped = 0, applied = 0;
  std::optional<body::ShapeDistance> distance;
  std::string error;
};

// Builds every project shape found in the source NIF and compares it with the same shape of `reference`.
std::vector<Comparison> build_and_compare(const nif::MeshFile& source, const nif::MeshFile& reference, const body::SliderProject& project,
                                          const body::DiffSets& data, const std::vector<body::SliderSetting>& preset, bool big) {
  std::vector<Comparison> out;
  for (const auto& ps : project.shapes) {
    Comparison c;
    c.shape = ps.name;
    const auto src = std::find_if(source.shapes.begin(), source.shapes.end(), [&](const auto& s) { return s.name == ps.name; });
    const auto ref = std::find_if(reference.shapes.begin(), reference.shapes.end(), [&](const auto& s) { return s.name == ps.name; });
    if (src == source.shapes.end() || ref == reference.shapes.end()) {
      c.error = src == source.shapes.end() ? "no está en el NIF fuente" : "no está en el NIF de referencia";
      out.push_back(std::move(c));
      continue;
    }
    auto shape = *src;
    const auto build = body::apply_project_sliders(shape, project, data, preset, big);
    c.applied = build.report.applied.size();
    c.zapped = build.zapped.size();
    auto positions = shape.positions;
    if (!build.zapped.empty()) {  // BodySlide deletes zapped vertices from the built mesh
      std::vector<nif::Vec3> kept;
      const std::set<std::uint16_t> zap(build.zapped.begin(), build.zapped.end());
      for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!zap.count(static_cast<std::uint16_t>(i))) kept.push_back(positions[i]);
      }
      positions = std::move(kept);
    }
    c.ours = positions.size();
    c.theirs = ref->positions.size();
    c.distance = body::compare_positions(positions, ref->positions, 1e-3f);
    if (!build.report.error.empty()) c.error = build.report.error;
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<body::SliderSetting> zeroed_preset(const body::SliderProject& project) {
  std::vector<body::SliderSetting> out;
  for (const auto& s : project.sliders) out.push_back({s.name, 0.0f, 0.0f});
  return out;
}

struct Loaded {
  body::SliderProject project;
  body::ProjectData data;
  nif::MeshFile source, reference;
};

std::optional<Loaded> load_build_inputs(const Args& a) {
  const auto roots = split_roots(a.get("--roots"));
  const auto osp_bytes = read_from_roots(roots, a.get("--osp"));
  if (!osp_bytes) {
    std::fprintf(stderr, "OSP no encontrado en las raíces: %s\n", a.get("--osp").c_str());
    return std::nullopt;
  }
  const auto projects = body::parse_slider_projects(text::decode_text(*osp_bytes), a.get("--osp"));
  const auto it = std::find_if(projects.begin(), projects.end(), [&](const auto& p) { return p.name == a.get("--set"); });
  if (it == projects.end()) {
    std::fprintf(stderr, "SliderSet «%s» no está en el OSP\n", a.get("--set").c_str());
    return std::nullopt;
  }
  Loaded l;
  l.project = *it;
  l.data = body::load_project_data(l.project, [&](const std::string& p) { return read_from_roots(roots, p); });
  const auto source_bytes = read_from_roots(roots, l.project.source_mesh());
  if (!source_bytes) {
    std::fprintf(stderr, "NIF fuente no encontrado: %s\n", l.project.source_mesh().c_str());
    return std::nullopt;
  }
  l.source = nif::load_mesh(*source_bytes, l.project.source_mesh());
  if (a.has("--compare")) {
    l.reference = nif::load_mesh(file_io::read_file(text::path_from_utf8(a.get("--compare")), 1024ull * 1024 * 1024), a.get("--compare"));
  }
  std::printf("proyecto «%s» (%s): fuente %s, %zu formas, %zu sliders, %zu conjuntos de datos, %zu archivos de datos ausentes, %zu errores\n",
              l.project.name.c_str(), l.project.file.c_str(), l.project.source_mesh().c_str(), l.project.shapes.size(),
              l.project.sliders.size(), l.data.sets.size(), l.data.missing.size(), l.data.errors.size());
  for (const auto& m : l.data.missing) std::printf("  falta: %s\n", m.c_str());
  for (const auto& e : l.data.errors) std::printf("  error: %s\n", e.c_str());
  return l;
}

void print_comparisons(const std::vector<Comparison>& cs) {
  for (const auto& c : cs) {
    if (c.distance) {
      std::printf("  %-28s verts %zu/%zu zap %zu sliders %zu  max %.6f  mean %.6f  >1e-3: %zu %s\n", c.shape.c_str(), c.ours, c.theirs,
                  c.zapped, c.applied, c.distance->max, c.distance->mean, c.distance->over_tolerance, c.error.c_str());
    } else {
      std::printf("  %-28s verts %zu/%zu zap %zu  NO COMPARABLE %s\n", c.shape.c_str(), c.ours, c.theirs, c.zapped, c.error.c_str());
    }
  }
}

// --zeroed, --preset-file + --preset, or the project's defaults.
std::optional<std::vector<body::SliderSetting>> select_preset(const Args& a, const body::SliderProject& project, std::string& label) {
  label = "valores por defecto del proyecto";
  if (a.has("--zeroed")) {
    label = "Zeroed Sliders";
    return zeroed_preset(project);
  }
  if (!a.has("--preset-file")) return std::vector<body::SliderSetting>{};
  const auto p = load_preset(a.get("--preset-file"), a.get("--preset"));
  if (!p) {
    std::fprintf(stderr, "preset «%s» no encontrado\n", a.get("--preset").c_str());
    return std::nullopt;
  }
  label = "preset " + p->name;
  return p->sliders;
}

void print_vectors(const char* what, const std::vector<nif::Vec3>& ours, const std::vector<nif::Vec3>& theirs, float& worst) {
  if (ours.empty() && theirs.empty()) return;
  const auto d = body::compare_positions(ours, theirs, 1e-3f);
  if (!d) {
    std::printf("      %-9s %zu frente a %zu: NO COMPARABLE\n", what, ours.size(), theirs.size());
    worst = 1e9f;
    return;
  }
  std::printf("      %-9s max %.6f mean %.6f >1e-3: %zu\n", what, d->max, d->mean, d->over_tolerance);
  worst = std::max(worst, d->max);
}

int write_command(const Args& a, const Loaded& loaded) {
  std::string label;
  const auto preset = select_preset(a, loaded.project, label);
  if (!preset) return 1;
  const auto source = read_from_roots(split_roots(a.get("--roots")), loaded.project.source_mesh());
  const auto build = body::build_slider_set(loaded.project, *source, loaded.data.sets, *preset, a.has("--morphs"));
  std::printf("build_slider_set (%s%s): %s\n", label.c_str(), a.has("--morphs") ? ", Build Morphs" : "", build.ok() ? "OK" : "ERROR");
  for (const auto& e : build.errors) std::printf("  error: %s\n", e.c_str());
  for (const auto& s : build.shapes) {
    std::printf("  %-28s verts %zu zap %zu sliders %zu desplazamiento max %.4f normales %s%s\n", s.shape.c_str(), s.vertices, s.zapped,
                s.sliders_applied, s.max_displacement, s.normals_recalculated ? "recalculadas" : "bloqueadas", s.deleted ? " (eliminada)" : "");
  }
  if (!build.ok()) return 1;
  if (build.vertex_count_changed() && build.tri.empty()) std::printf("AVISO: hay vértices eliminados (zap): el TRI existente ya no corresponde a esta malla\n");
  if (a.has("--out")) {
    const auto out_dir = text::path_from_utf8(a.get("--out"));
    const auto write_path = [&](const std::string& data_path, const std::string& bytes) {
      const auto path = out_dir / text::path_from_utf8(data_path);
      std::filesystem::create_directories(path.parent_path());
      file_io::write_file_atomic(path, bytes);
      std::printf("escrito %s (%zu bytes)\n", text::path_to_utf8(path).c_str(), bytes.size());
    };
    const auto write = [&](int variant, const std::string& bytes) { write_path(loaded.project.output_mesh(variant), bytes); };
    if (loaded.project.gen_weights) {
      write(0, build.small);
      write(1, build.big);
    } else {
      write(-1, build.big);
    }
    if (!build.tri.empty()) write_path(build.tri_path, build.tri);
  }
  if (!a.has("--compare")) return 0;
  const bool big = a.get("--big") != "0";
  const auto ours = nif::load_mesh(big || !loaded.project.gen_weights ? build.big : build.small, "build");
  std::printf("comparación del archivo escrito (%s) con %s\n", big ? "_1" : "_0", a.get("--compare").c_str());
  float worst = 0.0f;
  for (const auto& theirs : loaded.reference.shapes) {
    const auto mine = std::find_if(ours.shapes.begin(), ours.shapes.end(), [&](const auto& s) { return s.name == theirs.name; });
    if (mine == ours.shapes.end()) {
      std::printf("  %-28s falta en el archivo escrito\n", theirs.name.c_str());
      worst = 1e9f;
      continue;
    }
    std::printf("  %-28s verts %zu/%zu tris %zu/%zu particiones %s huesos %s\n", theirs.name.c_str(), mine->positions.size(),
                theirs.positions.size(), mine->indices.size() / 3, theirs.indices.size() / 3,
                mine->partitions == theirs.partitions ? "=" : "DISTINTAS", mine->bones == theirs.bones ? "=" : "DISTINTOS");
    print_vectors("posición", mine->positions, theirs.positions, worst);
    print_vectors("normal", mine->normals, theirs.normals, worst);
    print_vectors("tangente", mine->tangents, theirs.tangents, worst);
    print_vectors("bitang.", mine->bitangents, theirs.bitangents, worst);
    const auto uv3 = [](const std::vector<nif::Vec2>& uvs) {
      std::vector<nif::Vec3> out;
      for (const auto& uv : uvs) out.push_back({uv.u, uv.v, 0.0f});
      return out;
    };
    print_vectors("uv", uv3(mine->uvs), uv3(theirs.uvs), worst);
  }
  std::printf("RESULTADO peor diferencia %.6f\n", worst);
  return 0;
}

bool inside(const std::filesystem::path& folder, const std::filesystem::path& candidate) {
  const auto base = std::filesystem::weakly_canonical(folder);
  const auto path = std::filesystem::weakly_canonical(candidate);
  const auto [end, unused] = std::mismatch(base.begin(), base.end(), path.begin(), path.end());
  return !base.empty() && end == base.end();
}

std::string csv_field(std::string value) {
  std::replace(value.begin(), value.end(), ';', ',');
  std::replace(value.begin(), value.end(), '\n', ' ');
  return value;
}

// The first difference of shape names or vertex counts between a build and the mesh the profile has; "" when they match.
std::string topology_mismatch(const nif::MeshFile& built, const nif::MeshFile& current) {
  const auto counts = [](const nif::MeshFile& mesh) {
    std::map<std::string, std::size_t> out;
    for (const auto& s : mesh.shapes) out[s.name] = s.positions.size();
    return out;
  };
  const auto a = counts(built), b = counts(current);
  for (const auto& [name, vertices] : a) {
    const auto it = b.find(name);
    if (it == b.end()) return "forma nueva " + name;
    if (it->second != vertices) return name + ": " + std::to_string(vertices) + " vértices frente a " + std::to_string(it->second);
  }
  for (const auto& [name, vertices] : b) {
    if (!a.count(name)) return "falta la forma " + name;
  }
  for (const auto& mine : built.shapes) {
    const auto theirs = std::find_if(current.shapes.begin(), current.shapes.end(), [&](const auto& s) { return s.name == mine.name; });
    if (theirs != current.shapes.end() && mine.bones != theirs->bones) return mine.name + ": otros huesos";
  }
  return {};
}

int batch_command(const Args& a) {
  const auto info = mo2::instance_from_path(text::path_from_utf8(a.get("--instance")));
  if (!info) {
    std::fprintf(stderr, "instancia de MO2 no encontrada: %s\n", a.get("--instance").c_str());
    return 1;
  }
  const auto config = mo2::load_instance_config(*info);
  const auto out = text::path_from_utf8(a.get("--out"));
  std::error_code ec;
  if (a.get("--out").empty() || std::filesystem::exists(out, ec)) {
    std::fprintf(stderr, "--out debe ser una carpeta nueva: %s\n", a.get("--out").c_str());
    return 1;
  }
  for (const auto& forbidden : {config.base_dir, config.mods_dir, config.overwrite_dir, config.profiles_dir, config.game_path}) {
    if (inside(forbidden, out)) {
      std::fprintf(stderr, "No se escribe dentro de MO2 ni del juego: %s\n", text::path_to_utf8(forbidden).c_str());
      return 1;
    }
  }
  const auto profile_name = a.has("--profile") ? a.get("--profile") : config.selected_profile;
  const auto profile = mo2::load_profile(config, profile_name);
  const auto game = mo2::inspect_game(config.game_path);
  const auto vfs = mo2::VirtualFileSystem::build(config, profile, game);
  const auto library = body::load_preset_library(vfs);
  const auto preset = std::find_if(library.presets.begin(), library.presets.end(), [&](const auto& p) { return p.name == a.get("--preset"); });
  if (preset == library.presets.end()) {
    std::fprintf(stderr, "preset «%s» no está en el perfil «%s» (%zu presets)\n", a.get("--preset").c_str(), profile_name.c_str(), library.presets.size());
    return 1;
  }
  const auto index = body::load_project_index(vfs);
  const auto* reference = body::family_reference_project(index.projects, preset->sliders);
  if (!reference) {
    std::fprintf(stderr, "ningún proyecto del perfil usa los sliders del preset\n");
    return 1;
  }
  std::printf("perfil «%s», preset «%s» (%s), %zu proyectos; familia de referencia: «%s» (%zu sliders)\n", profile_name.c_str(), preset->name.c_str(),
              preset->provider.c_str(), index.projects.size(), reference->name.c_str(), reference->sliders.size());

  const body::DataReader read = [&](const std::string& path) -> std::optional<std::string> {
    const auto resolved = vfs.resolve(path);
    if (!resolved) return std::nullopt;
    return file_io::read_file(resolved->absolute, 1024ull * 1024 * 1024);
  };
  const auto mod_dir = out / "mod";
  std::filesystem::create_directories(mod_dir);
  std::string report = "set;osp;accion;salida;formas;sliders;desplazamiento_max;zap;detalle\n";
  std::map<std::string, int> totals;
  std::string in_overwrite;
  int overwrite_count = 0;
  const auto only = text::to_lower_ascii(a.get("--only"));
  // Several sets can build one mesh (variants of an outfit): the one whose shapes match the mesh the profile has now.
  std::map<std::string, const body::SliderProject*> chosen;
  std::map<std::string, std::size_t> chosen_overlap;
  for (const auto& project : index.projects) {
    {
      const auto action = body::decide_batch_set(project, *reference, preset->sliders).action;
      if (action != "build" && !(action == "ambiguous" && a.has("--include-ambiguous"))) continue;
    }
    const auto output = project.output_mesh(project.gen_weights ? 1 : -1);
    if (index.for_mesh(output).size() < 2) {
      chosen[output] = &project;
      continue;
    }
    std::size_t overlap = 0;
    try {
      if (const auto current = read(output)) {
        const auto mesh = nif::load_mesh(*current, output);
        for (const auto& ps : project.shapes) {
          overlap += std::any_of(mesh.shapes.begin(), mesh.shapes.end(), [&](const auto& s) { return s.name == ps.name; }) ? 1 : 0;
        }
      }
    } catch (const std::exception&) {
    }
    if (!chosen.count(output) || overlap > chosen_overlap[output]) {
      chosen[output] = &project;
      chosen_overlap[output] = overlap;
    }
  }
  std::size_t done = 0;
  for (const auto& project : index.projects) {
    ++done;
    const auto output = project.output_mesh(project.gen_weights ? 1 : -1);
    if (!only.empty() && text::to_lower_ascii(project.name + " " + output).find(only) == std::string::npos) continue;
    std::string action, detail;
    std::size_t shapes = 0, sliders = 0, zapped = 0;
    float moved = 0.0f;
    try {
      auto decision = body::decide_batch_set(project, *reference, preset->sliders);
      if (decision.action == "ambiguous" && a.has("--include-ambiguous")) decision.action = "build";
      action = decision.action;
      detail = decision.reason;
      if (action == "build" && chosen.at(output) != &project) {
        action = "duplicate-output";
        detail = "Esta malla la construye «" + chosen.at(output)->name + "», que coincide con la del perfil.";
      } else if (action == "build") {
        const auto data = body::load_project_data(project, read);
        const auto source = read(project.source_mesh());
        if (!source) {
          action = "error";
          detail = "Falta el NIF fuente: " + project.source_mesh();
        } else {
          const auto build = body::build_slider_set(project, *source, data.sets, preset->sliders);
          if (!build.ok()) {
            action = "error";
            detail = build.errors.empty() ? "sin salida" : build.errors.front();
          } else if (const auto current = read(output); !current) {
            action = "no-current-mesh";
            detail = "El perfil no tiene esta malla suelta: no hay nada que reemplazar.";
          } else if (const auto mismatch = topology_mismatch(nif::load_mesh(build.big, output), nif::load_mesh(*current, output)); !mismatch.empty()) {
            action = "topology-differs";
            detail = "Otra versión que la malla del perfil (" + mismatch + "): reemplazarla podría romper a quien la usa.";
          } else {
            const auto write = [&](int variant, const std::string& bytes) {
              const auto path = mod_dir / text::path_from_utf8(project.output_mesh(variant));
              // MO2's Overwrite wins over every mod: a mesh there hides this build until it is moved out.
              if (const auto winner = vfs.resolve(project.output_mesh(variant));
                  winner && vfs.providers()[winner->provider].kind == mo2::ProviderKind::Overwrite) {
                in_overwrite += project.output_mesh(variant) + "\n";
                ++overwrite_count;
              }
              std::filesystem::create_directories(path.parent_path());
              file_io::write_file_atomic(path, bytes);
            };
            if (project.gen_weights) write(0, build.small);
            write(project.gen_weights ? 1 : -1, build.big);
            for (const auto& s : build.shapes) {
              ++shapes;
              sliders = std::max(sliders, s.sliders_applied);
              zapped += s.zapped;
              moved = std::max(moved, s.max_displacement);
            }
            if (!data.missing.empty()) detail = std::to_string(data.missing.size()) + " archivos de datos ausentes: " + data.missing.front();
          }
        }
      }
    } catch (const std::exception& e) {
      action = "error";
      detail = e.what();
    }
    ++totals[action];
    report += csv_field(project.name) + ";" + csv_field(project.file) + ";" + action + ";" + csv_field(output) + ";" + std::to_string(shapes) + ";" +
              std::to_string(sliders) + ";" + std::to_string(moved) + ";" + std::to_string(zapped) + ";" + csv_field(detail) + "\n";
    if (action == "build" || action == "error") {
      std::printf("[%zu/%zu] %s: %s\n", done, index.projects.size(), action.c_str(), project.name.c_str());
      std::fflush(stdout);
    }
  }
  file_io::write_file_atomic(out / "informe.csv", report);
  std::printf("RESULTADO");
  for (const auto& [action, count] : totals) std::printf(" %s=%d", action.c_str(), count);
  std::printf("\ninforme: %s\n", text::path_to_utf8(out / "informe.csv").c_str());
  if (overwrite_count) {
    file_io::write_file_atomic(out / "en-overwrite.txt", in_overwrite);
    std::printf("ATENCION: %d mallas siguen ganando desde Overwrite y taparian este mod: %s\n", overwrite_count, text::path_to_utf8(out / "en-overwrite.txt").c_str());
  }
  return totals["error"] ? 3 : 0;
}

}  // namespace

int bodyslide_command(int argc, char** argv) {
  const std::string command = argv[1];
  const auto a = parse_args(argc, argv, 2);
  if (command == "bodyslide-batch") return batch_command(a);
  if (command == "bodyslide-build" || command == "bodyslide-match" || command == "bodyslide-write") {
    const auto loaded = load_build_inputs(a);
    if (!loaded) return 1;
    if (command == "bodyslide-write") return write_command(a, *loaded);
    const bool big = a.get("--big") != "0";
    if (command == "bodyslide-build") {
      std::string label;
      const auto chosen = select_preset(a, loaded->project, label);
      if (!chosen) return 1;
      const auto& preset = *chosen;
      std::printf("build %s (%s) frente a %s\n", big ? "_1" : "_0", label.c_str(), a.get("--compare").c_str());
      const auto cs = build_and_compare(loaded->source, loaded->reference, loaded->project, loaded->data.sets, preset, big);
      print_comparisons(cs);
      float worst = 0;
      bool all = true;
      for (const auto& c : cs) {
        if (c.distance) worst = std::max(worst, c.distance->max);
        else all = false;
      }
      std::printf("RESULTADO max %.6f %s\n", worst, all ? "(todas las formas comparadas)" : "(hay formas no comparables)");
      return 0;
    }
    // match: every preset of the list, plus zeroed sliders and the project defaults.
    struct Score {
      std::string name;
      float max = 0, mean = 0;
    };
    std::vector<Score> scores;
    auto score = [&](const std::string& name, const std::vector<body::SliderSetting>& preset) {
      const auto cs = build_and_compare(loaded->source, loaded->reference, loaded->project, loaded->data.sets, preset, big);
      Score s{name, 0, 0};
      std::size_t n = 0;
      for (const auto& c : cs) {
        if (!c.distance) continue;
        s.max = std::max(s.max, c.distance->max);
        s.mean += c.distance->mean;
        ++n;
      }
      if (n) s.mean /= static_cast<float>(n);
      if (n) scores.push_back(s);
    };
    score("<Zeroed Sliders>", zeroed_preset(loaded->project));
    score("<valores por defecto>", {});
    const auto list = file_io::read_text_file(text::path_from_utf8(a.get("--presets-list")));
    std::size_t presets = 0;
    for (const auto line : text::split_lines(list)) {
      const auto path = std::string(text::trim(line));
      if (path.empty()) continue;
      try {
        const auto parsed = body::parse_slider_presets(file_io::read_text_file(text::path_from_utf8(path)), path, "probe");
        for (const auto& p : parsed.presets) {
          ++presets;
          score(p.name + "  [" + path + "]", p.sliders);
        }
      } catch (const std::exception& e) {
        std::fprintf(stderr, "preset ilegible %s: %s\n", path.c_str(), e.what());
      }
    }
    std::sort(scores.begin(), scores.end(), [](const auto& x, const auto& y) { return x.mean < y.mean; });
    std::printf("%zu presets evaluados; mejores coincidencias (media de la distancia media por forma):\n", presets);
    for (std::size_t i = 0; i < scores.size() && i < 8; ++i) {
      std::printf("  %2zu. mean %.6f max %.6f  %s\n", i + 1, scores[i].mean, scores[i].max, scores[i].name.c_str());
    }
    return 0;
  }
  if (command == "fit-check") {
    // sns-probe fit-check --body <file.nif> --body-shape <name> --garment <file.nif> [--garment-shape <name>] [--reach 3] [--tolerance 0.1]
    const auto body_mesh = nif::load_mesh(file_io::read_file(text::path_from_utf8(a.get("--body")), 1024ull * 1024 * 1024), a.get("--body"));
    const auto garment_mesh = nif::load_mesh(file_io::read_file(text::path_from_utf8(a.get("--garment")), 1024ull * 1024 * 1024), a.get("--garment"));
    const auto body_shape = std::find_if(body_mesh.shapes.begin(), body_mesh.shapes.end(), [&](const auto& s) { return s.name == a.get("--body-shape"); });
    if (body_shape == body_mesh.shapes.end()) {
      std::fprintf(stderr, "forma de cuerpo «%s» no encontrada\n", a.get("--body-shape").c_str());
      return 1;
    }
    const float reach = a.has("--reach") ? std::stof(a.get("--reach")) : 3.0f;
    const float tolerance = a.has("--tolerance") ? std::stof(a.get("--tolerance")) : 0.1f;
    std::printf("cuerpo: %s [%s], %zu vértices\n", a.get("--body").c_str(), body_shape->name.c_str(), body_shape->positions.size());
    for (const auto& g : garment_mesh.shapes) {
      if (a.has("--garment-shape") && g.name != a.get("--garment-shape")) continue;
      const auto r = body::measure_penetration(*body_shape, g, reach, tolerance);
      std::printf("  %-20s vértices %zu, cerca del cuerpo %zu, DENTRO (>%.2f) %zu, profundidad max %.4f, holgura media %.4f\n", g.name.c_str(),
                  r.garment_vertices, r.near_body, tolerance, r.inside, r.max_depth, r.mean_gap);
    }
    return 0;
  }
  if (command == "tri-apply") {
    auto mesh = nif::load_mesh(file_io::read_file(text::path_from_utf8(a.get("--nif")), 1024ull * 1024 * 1024), a.get("--nif"));
    const auto tri = body::parse_tri(file_io::read_file(text::path_from_utf8(a.get("--tri")), 1024ull * 1024 * 1024), a.get("--tri"));
    const auto p = load_preset(a.get("--preset-file"), a.get("--preset"));
    if (!p) {
      std::fprintf(stderr, "preset «%s» no encontrado\n", a.get("--preset").c_str());
      return 1;
    }
    const float weight = a.has("--weight") ? std::stof(a.get("--weight")) : 100.0f;
    std::optional<nif::MeshFile> compare;
    if (a.has("--compare")) {
      compare = nif::load_mesh(file_io::read_file(text::path_from_utf8(a.get("--compare")), 1024ull * 1024 * 1024), a.get("--compare"));
    }
    std::printf("TRI %s: %zu formas; preset «%s» (%zu sliders) a peso %.0f\n", a.get("--tri").c_str(), tri.shapes.size(), p->name.c_str(),
                p->sliders.size(), weight);
    for (auto& shape : mesh.shapes) {
      const auto* morphs = tri.shape(shape.name);
      if (!morphs) {
        std::printf("  %-28s sin morphs en el TRI\n", shape.name.c_str());
        continue;
      }
      const auto r = body::apply_tri_morphs(shape, *morphs, p->sliders, weight);
      const auto cov = body::slider_coverage(r);
      std::printf("  %-28s %zu verts, %zu morphs, %zu compatibles, %zu aplicados, %zu sin morph, cobertura %.0f%%, max %.4f %s\n",
                  shape.name.c_str(), r.vertex_count, r.morphs_available, r.compatible, r.applied.size(), r.not_in_data.size(),
                  cov.ratio * 100.0f, r.max_displacement, r.error.c_str());
      if (!r.not_in_data.empty()) {
        std::string names;
        for (const auto& n : r.not_in_data) names += (names.empty() ? "" : ", ") + n;
        std::printf("      sin morph: %s\n", names.c_str());
      }
      if (compare) {
        const auto ref = std::find_if(compare->shapes.begin(), compare->shapes.end(), [&](const auto& s) { return s.name == shape.name; });
        if (ref != compare->shapes.end()) {
          const auto d = body::compare_positions(shape.positions, ref->positions, 1e-2f);
          if (d) std::printf("      frente a %s: max %.6f mean %.6f >1e-2: %zu\n", a.get("--compare").c_str(), d->max, d->mean, d->over_tolerance);
          else std::printf("      frente a la referencia: distinto número de vértices\n");
        }
      }
    }
    return 0;
  }
  return 2;
}
