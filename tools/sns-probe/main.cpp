// sns-probe: read-only diagnostics over game archives and meshes.
//
//   sns-probe bsa-list <archive.bsa> [substring]
//   sns-probe bsa-extract <archive.bsa> <data path> <output file>
//   sns-probe nif-dump <file.nif> [raw vertex dump directory]
//
// It never writes next to the inputs: every output path is chosen by the caller.
#include <NifFile.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "sns/assets/bsa_archive.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/face/facegen.hpp"
#include "sns/textures/dds.hpp"

namespace {

int usage() {
  std::fputs(
      "usage:\n"
      "  sns-probe bsa-list <archive.bsa> [substring]\n"
      "  sns-probe bsa-extract <archive.bsa> <data path> <output file>\n"
      "  sns-probe nif-dump <file.nif> [raw vertex dump directory]\n"
      "  sns-probe dds-raw <file.dds> <output.rgba>  (mip 0 decoded to RGBA8)\n",
      stderr);
  return 2;
}

void print_transform(const char* label, const nifly::MatTransform& t) {
  std::printf("    %s: t=(%.5f %.5f %.5f) s=%.5f r=[%.5f %.5f %.5f | %.5f %.5f %.5f | %.5f %.5f %.5f]\n", label,
              t.translation.x, t.translation.y, t.translation.z, t.scale, t.rotation[0].x, t.rotation[0].y,
              t.rotation[0].z, t.rotation[1].x, t.rotation[1].y, t.rotation[1].z, t.rotation[2].x, t.rotation[2].y,
              t.rotation[2].z);
}

int nif_dump(const std::filesystem::path& path, const std::filesystem::path& raw_dir) {
  nifly::NifFile nif;
  if (nif.Load(path) != 0) {
    std::fprintf(stderr, "cannot load NIF\n");
    return 1;
  }
  auto* root = nif.GetRootNode();
  std::printf("root: %s (%s), blocks=%u\n", root ? root->name.get().c_str() : "-", root ? root->GetBlockName() : "-",
              nif.GetHeader().GetNumBlocks());
  for (auto* shape : nif.GetShapes()) {
    std::vector<nifly::Vector3> verts;
    nif.GetVertsForShape(shape, verts);
    std::vector<nifly::Triangle> tris;
    shape->GetTriangles(tris);
    std::printf("shape '%s' (%s) verts=%zu tris=%zu skinned=%d\n", shape->name.get().c_str(), shape->GetBlockName(),
                verts.size(), tris.size(), shape->IsSkinned() ? 1 : 0);
    print_transform("transform", shape->GetTransformToParent());
    if (auto* parent = nif.GetParentNode(shape)) std::printf("    parent: %s\n", parent->name.get().c_str());
    if (auto* shader = nif.GetShader(shape)) {
      std::printf("    shader: %s type=%u modelSpace=%d\n", shader->GetBlockName(), shader->GetShaderType(),
                  shader->IsModelSpace() ? 1 : 0);
      if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(shader)) {
        std::printf("    flags1=%08X flags2=%08X skinTint=(%.4f %.4f %.4f) a=%.4f hairTint=(%.4f %.4f %.4f) alpha=%.3f\n",
                    lighting->shaderFlags1, lighting->shaderFlags2, lighting->skinTintColor.x, lighting->skinTintColor.y,
                    lighting->skinTintColor.z, lighting->skinTintAlpha, lighting->hairTintColor.x,
                    lighting->hairTintColor.y, lighting->hairTintColor.z, lighting->alpha);
      }
      for (std::uint32_t slot = 0; slot < 9; ++slot) {
        std::string tex;
        nif.GetTextureSlot(shape, tex, slot);
        if (!tex.empty()) std::printf("    tex[%u]: %s\n", slot, tex.c_str());
      }
    }
    if (auto* alpha = nif.GetAlphaProperty(shape)) std::printf("    alpha: flags=%u threshold=%u\n", alpha->flags, alpha->threshold);
    std::vector<std::string> bones;
    nif.GetShapeBoneList(shape, bones);
    if (!bones.empty()) {
      std::printf("    bones (%zu):", bones.size());
      for (const auto& b : bones) std::printf(" [%s]", b.c_str());
      std::printf("\n");
      nifly::MatTransform global_to_skin;
      if (nif.GetShapeTransformGlobalToSkin(shape, global_to_skin)) print_transform("globalToSkin", global_to_skin);
      for (std::uint32_t i = 0; i < bones.size(); ++i) {
        nifly::MatTransform skin_to_bone;
        if (nif.GetShapeTransformSkinToBone(shape, i, skin_to_bone)) print_transform(bones[i].c_str(), skin_to_bone);
      }
    }
    if (!raw_dir.empty()) {
      std::filesystem::create_directories(raw_dir);
      std::ofstream out(raw_dir / (shape->name.get() + ".verts.bin"), std::ios::binary);
      out.write(reinterpret_cast<const char*>(verts.data()), static_cast<std::streamsize>(verts.size() * sizeof(nifly::Vector3)));
      std::vector<nifly::Vector3> normals;
      if (const auto* n = nif.GetNormalsForShape(shape)) normals = *n;
      std::ofstream norm_out(raw_dir / (shape->name.get() + ".norms.bin"), std::ios::binary);
      norm_out.write(reinterpret_cast<const char*>(normals.data()), static_cast<std::streamsize>(normals.size() * sizeof(nifly::Vector3)));
      if (auto* dyn = dynamic_cast<nifly::BSDynamicTriShape*>(shape)) {
        std::ofstream dyn_out(raw_dir / (shape->name.get() + ".dyn.bin"), std::ios::binary);
        dyn_out.write(reinterpret_cast<const char*>(dyn->dynamicData.data()), static_cast<std::streamsize>(dyn->dynamicData.size() * sizeof(nifly::Vector4)));
      }
      std::ofstream tri_out(raw_dir / (shape->name.get() + ".tris.bin"), std::ios::binary);
      tri_out.write(reinterpret_cast<const char*>(tris.data()), static_cast<std::streamsize>(tris.size() * sizeof(nifly::Triangle)));
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string_view command = argv[1];
  try {
    if (command == "dds-raw" && argc == 4) {
      const auto bytes = sns::file_io::read_file(sns::text::path_from_utf8(argv[2]), 512ull * 1024 * 1024);
      const auto image = sns::textures::parse_dds(bytes);
      const auto rgba = sns::textures::decode_rgba(image);
      sns::file_io::write_file_atomic(sns::text::path_from_utf8(argv[3]), rgba.data);
      std::printf("%u %u %s mips=%u srgb=%d\n", rgba.width, rgba.height, std::string(sns::textures::to_string(image.format)).c_str(),
                  image.mip_count, image.srgb ? 1 : 0);
      return 0;
    }
    if (command == "facegen-build" && argc == 5) {
      // facegen-build <recipe.json> <output.nif> <Data folder>: vanilla archives + loose files.
      const auto text_json = sns::file_io::read_text_file(sns::text::path_from_utf8(argv[2]));
      std::fprintf(stderr, "read %zu bytes\n", text_json.size());
      const auto parsed = nlohmann::json::parse(text_json);
      std::fprintf(stderr, "parsed json\n");
      const auto recipe = sns::face::recipe_from_json(parsed);
      std::fprintf(stderr, "recipe ok\n");
      const auto data = sns::text::path_from_utf8(argv[4]);
      std::vector<std::unique_ptr<sns::assets::BsaArchive>> archives;
      for (const auto& entry : std::filesystem::directory_iterator(data)) {
        const auto name = sns::text::to_lower_ascii(sns::text::path_to_utf8(entry.path().filename()));
        if (name.starts_with("skyrim - meshes") || name.starts_with("skyrim - textures")) {
          archives.push_back(sns::assets::BsaArchive::open(entry.path()));
        }
      }
      sns::face::FaceAssetCache cache([&](std::string_view p) -> std::optional<std::string> {
        const auto key = sns::text::data_path_key(p);
        std::error_code ec;
        if (const auto loose = data / sns::text::path_from_utf8(key); std::filesystem::is_regular_file(loose, ec)) {
          return sns::file_io::read_file(loose, 512ull * 1024 * 1024);
        }
        for (const auto& a : archives) {
          if (auto bytes = a->read(key)) return bytes;
        }
        return std::nullopt;
      });
      std::printf("building %zu parts\n", recipe.parts.size());
      std::fflush(stdout);
      const auto build = sns::face::build_facegen(recipe, cache);
      for (const auto& part : build.parts) {
        std::printf("part %s: %s (%s) %zu verts, %zu morphs applied, %zu missing\n", part.editor_id.c_str(), part.status.c_str(),
                    part.geometry_source.c_str(), part.vertex_count, part.applied.size(), part.missing.size());
      }
      for (const auto& i : build.issues) std::printf("%s %s: %s [%s]\n", i.severity.c_str(), i.code.c_str(), i.message.c_str(), i.subject.c_str());
      if (build.nif.empty()) return 1;
      sns::file_io::write_file_atomic(sns::text::path_from_utf8(argv[3]), build.nif);
      std::printf("wrote %zu bytes\n", build.nif.size());
      return 0;
    }
    if (command == "nif-tree") {
      nifly::NifFile nif;
      if (nif.Load(sns::text::path_from_utf8(argv[2])) != 0) return 1;
      std::function<void(nifly::NiObject*, int)> walk = [&](nifly::NiObject* block, int depth) {
        if (!block) return;
        auto* av = dynamic_cast<nifly::NiAVObject*>(block);
        std::printf("%*s%s '%s' flags=%u\n", depth * 2, "", block->GetBlockName(), av ? av->name.get().c_str() : "",
                    av ? av->flags : 0u);
        if (auto* node = dynamic_cast<nifly::NiNode*>(block)) {
          for (auto& child : node->childRefs) walk(nif.GetHeader().GetBlock<nifly::NiObject>(child), depth + 1);
        }
        if (auto* av_obj = dynamic_cast<nifly::NiObjectNET*>(block)) {
          for (auto& extra : av_obj->extraDataRefs) {
            if (auto* e = nif.GetHeader().GetBlock<nifly::NiExtraData>(extra)) {
              std::printf("%*s[extra] %s '%s'\n", depth * 2 + 2, "", e->GetBlockName(), e->name.get().c_str());
            }
          }
        }
      };
      walk(nif.GetRootNode(), 0);
      return 0;
    }
    if (command == "nif-dump") {
      return nif_dump(sns::text::path_from_utf8(argv[2]), argc > 3 ? sns::text::path_from_utf8(argv[3]) : std::filesystem::path());
    }
    const auto archive = sns::assets::BsaArchive::open(sns::text::path_from_utf8(argv[2]));
    if (command == "bsa-list") {
      const std::string filter = argc > 3 ? sns::text::to_lower_ascii(argv[3]) : std::string();
      for (const auto& entry : archive->list()) {
        if (filter.empty() || entry.find(filter) != std::string::npos) std::printf("%s\n", entry.c_str());
      }
      return 0;
    }
    if (command == "bsa-extract" && argc == 5) {
      const auto bytes = archive->read(sns::text::data_path_key(argv[3]));
      if (!bytes) {
        std::fprintf(stderr, "not in archive: %s\n", argv[3]);
        return 1;
      }
      sns::file_io::write_file_atomic(sns::text::path_from_utf8(argv[4]), *bytes);
      std::printf("%zu bytes\n", bytes->size());
      return 0;
    }
    return usage();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
