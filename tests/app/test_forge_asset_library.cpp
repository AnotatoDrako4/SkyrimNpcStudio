#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "forge_asset_library.hpp"
#include "sns/creation/forge_project.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using sns::app::forge_assets::import_model;
using sns::app::forge_assets::resolve_model;

namespace {
QString qs(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
}

TEST_CASE("Forge managed GLB is portable, deduplicated and integrity checked", "[forge][assets]") {
  sns::test::TempDir temp;
  const auto project = temp.path() / "Portable.sncf";
  sns::creation::ForgeProjectStore::save(project, sns::creation::ForgeProjectStore::create("Portable"));
  const auto source = fs::path(SNS_TEST_MODELS_DIR) / "suzanne.glb";
  const auto first = import_model(qs(project), qs(source));
  CHECK_FALSE(first.duplicate);
  CHECK(first.sha256.size() == 64);
  CHECK(first.relative_path.startsWith("assets/models/"));
  const auto managed = project / fs::path(first.relative_path.toStdWString());
  REQUIRE(fs::is_regular_file(managed));
  CHECK(resolve_model(qs(project), first.relative_path, first.sha256) == qs(fs::canonical(managed)));
  const auto second = import_model(qs(project), qs(source));
  CHECK(second.duplicate);
  CHECK(second.relative_path == first.relative_path);

  const auto moved = temp.path() / "Moved.sncf";
  fs::copy(project, moved, fs::copy_options::recursive);
  CHECK_FALSE(resolve_model(qs(moved), first.relative_path, first.sha256).isEmpty());
  { std::ofstream out(moved / fs::path(first.relative_path.toStdWString()), std::ios::binary | std::ios::app); out << 'x'; }
  CHECK(resolve_model(qs(moved), first.relative_path, first.sha256).isEmpty());
  CHECK(resolve_model(qs(project), "../outside.glb", first.sha256).isEmpty());
}

TEST_CASE("Forge managed glTF copies validated external buffers and textures", "[forge][assets]") {
  sns::test::TempDir temp;
  const auto project = temp.path() / "Split.sncf";
  sns::creation::ForgeProjectStore::save(project, sns::creation::ForgeProjectStore::create("Split"));
  const auto source = fs::path(SNS_TEST_MODELS_DIR) / "rigged_figure_split" / "rigged_figure.gltf";
  const auto imported = import_model(qs(project), qs(source));
  REQUIRE_FALSE(resolve_model(qs(project), imported.relative_path, imported.sha256).isEmpty());
  const auto folder = (project / fs::path(imported.relative_path.toStdWString())).parent_path();
  CHECK(fs::is_regular_file(folder / "rigged_figure.bin"));
  CHECK(fs::is_regular_file(folder / "FigureGrid.png"));
  fs::remove(folder / "rigged_figure.bin");
  CHECK(resolve_model(qs(project), imported.relative_path, imported.sha256).isEmpty());
}
