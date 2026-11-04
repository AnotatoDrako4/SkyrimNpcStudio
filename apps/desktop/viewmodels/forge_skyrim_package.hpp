#pragma once

#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

namespace sns::app::forge_skyrim {

// "Goblet de latón!" -> "goblet_de_lat_n" (lowercase ASCII, digits, '_'; never empty, at most 48 characters).
QString slug(const QString& text);

// Turns a finished "exportNif" stage of tools/blender/forge_skyrim.py into a Type A resource folder:
//   <project.sncf>/exports/skyrim/<name>-<UTC stamp>/
//     Data/meshes/<prefix>/<name>.nif            the PyNifly NIF, byte for byte
//     Data/textures/<prefix>/<image>.dds         every staged PNG encoded BC1 (opaque) or BC3 (alpha), full mips
//     report.json, verification.json, manifest.json (sha256 + bytes of every file)
// The NIF is re-read first (creation::skyrim::verify_nif); when a check fails nothing is written and the result
// says why. Written atomically (.partial-<uuid> then rename); nothing is written outside the .sncf project.
// -> {packaged, folder, verification, classification, manifest, skyrimStatus: "PENDIENTE_SKYRIM"}
// Throws std::invalid_argument / std::runtime_error for an unusable stage or project.
QVariantMap package_export(const QString& project_folder, const QString& stage, const nlohmann::json& report,
                           const QString& name);

// The worker request (export.forgeStaticResource) that turns a folder written by package_export into an MO2 mod: plugin
// "CF_<name>.esp" with one STAT "CF_<name>" whose model is the packaged NIF and whose OBND is the nifly-read box rounded outwards;
// every Data/ file of the manifest with its SHA-256 (the worker re-hashes them). Only reads the package (its manifest and
// verification.json must say the NIF was verified). Throws std::invalid_argument when the folder is not such a package.
nlohmann::json mod_request(const QString& package_folder, const QString& output_directory);

// SkinnedMesh reference skeleton, read only: SNS_FORGE_REFERENCE_SKELETON, else the Skyrim skeleton.nif shipped with the
// PyNifly add-on that ran the job (report.pynifly.path/skeletons/Skyrim/skeleton.nif). Empty when none exists.
QString default_reference_skeleton(const nlohmann::json& report);

// Reads the reference skeleton and stores creation::skyrim::map_skeleton of the armature that binds the report's skinned
// meshes (prepared scene when present) as report["skeletonMapping"]. An empty path removes any previous mapping, so the
// classification reports noReferenceSkeleton. Throws sns::Error / std::runtime_error for an unreadable skeleton.
void attach_skeleton_mapping(nlohmann::json& report, const QString& skeleton_path, const QString& label = {});

}  // namespace sns::app::forge_skyrim
