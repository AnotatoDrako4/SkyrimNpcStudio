#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>
#include <string>
#include <vector>

#include "mo2_snapshot.hpp"
#include "sns/appearance/appearance_draft.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/face/face_recipe.hpp"
#include "sns/face/facegen.hpp"

namespace sns::app {

// Deep asset & compatibility inspection of a single HeadPart (Hair, Eyes, Eyebrows, FacialHair, Scar, Face).
QVariantMap inspect_head_part_asset(const QVariantMap& head_part_option,
                                    const face::FaceRecipe* recipe,
                                    const Mo2Snapshot* snapshot,
                                    const assets::AssetResolver* resolver,
                                    face::FaceAssetCache* face_assets = nullptr);

// Filters and annotates HeadPart options for Hair Studio / Face Beauty Workshop.
QVariantList filter_head_part_options(const QVariantList& options,
                                      const QString& category,
                                      const QString& search_text,
                                      const QString& package_filter,
                                      const QString& compatibility_filter,
                                      const QStringList& favorite_form_keys);

// Evaluates Face Beauty Workshop capabilities: parametric morphs vs imported/locked geometry and technical limits.
QVariantMap evaluate_workshop_capabilities(const face::FaceRecipe* recipe,
                                           const QVariantMap& face_compatibility,
                                           const QString& geometry_mode);

// Compares Original (record baseline) vs Proposed (current draft) appearance, MO2 provenance, and export readiness.
QVariantMap compare_original_vs_proposed(const face::FaceRecipe* base_recipe,
                                         const face::FaceRecipe* current_recipe,
                                         const appearance::FaceState& baseline,
                                         const appearance::FaceState& proposed,
                                         float baseline_height,
                                         float proposed_height,
                                         const QVariantMap& face_compatibility,
                                         const QVariantMap& facegen_report,
                                         bool exportable,
                                         const Mo2Snapshot* snapshot,
                                         const assets::AssetResolver* resolver);

// Returns the Next-Gen Visual Preset catalog (Vanilla Remastered, Lore Friendly HD, Cinematic Fantasy).
QVariantList next_gen_style_presets();

struct StyleApplicationResult {
  appearance::FaceState next_state;
  bool changed = false;
  QString style_id;
  QString style_name;
  QString summary;
  QStringList applied_rules;
  QStringList skipped_rules;
};

// Evaluates and applies a Next-Gen Visual Preset style to the current NPC draft state.
StyleApplicationResult apply_next_gen_style(const QString& style_id,
                                             const QVariantMap& options,
                                             const face::FaceRecipe& recipe,
                                             const appearance::FaceState& baseline,
                                             const appearance::FaceState& current,
                                             const QVariantList& head_part_options,
                                             const QVariantMap& face_compatibility);

// Runs the deterministic Visual Quality Check (geometric metrics + asset resolution + preset fidelity).
QVariantMap run_visual_quality_check(const face::FaceRecipe* recipe,
                                     const appearance::FaceState& baseline,
                                     const appearance::FaceState& current,
                                     bool intentionally_bald,
                                     const QVariantMap& face_compatibility,
                                     const QVariantMap& facegen_report,
                                     const QVariantList& scene_parts,
                                     const QVariantMap& scene_stats,
                                     const QVariantList& scene_issues,
                                     const QVariantMap& skin_match,
                                     const Mo2Snapshot* snapshot,
                                     const assets::AssetResolver* resolver);

// Persistent storage for NPC Beauty Library (presets, favorite hairs, head+eye combos, facial configs, resource packs).
class BeautyLibraryStore {
 public:
  explicit BeautyLibraryStore(QString directory = {});

  [[nodiscard]] static QString default_directory();
  [[nodiscard]] QString directory() const { return dir_; }
  [[nodiscard]] QString file_path() const;
  [[nodiscard]] QString previews_directory() const;

  bool load(QString* error = nullptr);
  bool save(QString* error = nullptr) const;

  [[nodiscard]] QVariantList presets() const { return presets_; }
  [[nodiscard]] QVariantList favorite_hairs() const { return favorite_hairs_; }
  [[nodiscard]] QVariantList head_eye_combos() const { return head_eye_combos_; }
  [[nodiscard]] QVariantList facial_configs() const { return facial_configs_; }
  [[nodiscard]] QVariantList imported_resources() const { return imported_resources_; }
  [[nodiscard]] QStringList favorite_hair_keys() const;

  // Saves a preview image into previews/ and returns its absolute path.
  QString save_preview_image(const QString& id, const QImage& image) const;

  // Presets / Proposals
  QString save_preset(const QString& name,
                      const QString& category,
                      const QString& notes,
                      const face::FaceRecipe& recipe,
                      const appearance::FaceState& state,
                      float height,
                      bool intentionally_bald,
                      const QString& export_state,
                      const QString& export_state_reason,
                      const QVariantList& associated_resources,
                      const QString& preview_image_path = {},
                      QString* error = nullptr);
  bool delete_preset(const QString& id, QString* error = nullptr);
  [[nodiscard]] QVariantMap find_preset(const QString& id) const;

  // Favorite hairs
  bool toggle_favorite_hair(const QVariantMap& hair_info, QString* error = nullptr);
  bool is_favorite_hair(const QString& form_key) const;

  // Head + Eye combinations
  QString save_head_eye_combo(const QString& name,
                              const face::FaceRecipe& recipe,
                              const appearance::FaceState& state,
                              const QVariantList& associated_resources,
                              const QString& preview_image_path = {},
                              QString* error = nullptr);
  bool delete_head_eye_combo(const QString& id, QString* error = nullptr);

  // Facial configurations (morphs + NAMA presets + tints + skinTone)
  QString save_facial_config(const QString& name,
                             const face::FaceRecipe& recipe,
                             const appearance::FaceState& state,
                             const QString& preview_image_path = {},
                             QString* error = nullptr);
  bool delete_facial_config(const QString& id, QString* error = nullptr);

  // Import external compatible resource metadata (.jslot or .json preset) without redistributing protected binaries.
  QVariantMap import_compatible_resource(const QString& file_path,
                                         const QString& category_tag,
                                         QString* error = nullptr);
  bool delete_imported_resource(const QString& id, QString* error = nullptr);

  // Evaluates compatibility of a saved library item against the currently loaded NPC and MO2 snapshot.
  [[nodiscard]] QVariantMap evaluate_entry_compatibility(const QVariantMap& entry,
                                                         const face::FaceRecipe* target_recipe,
                                                         const QVariantMap& face_compatibility,
                                                         const Mo2Snapshot* snapshot) const;

  // Reconstructs a FaceState from a saved preset, head+eye combo, or facial config on top of `current`.
  [[nodiscard]] std::optional<appearance::FaceState> apply_entry_to_state(const QVariantMap& entry,
                                                                          const face::FaceRecipe& target_recipe,
                                                                          const appearance::FaceState& current,
                                                                          const QVariantMap& face_compatibility,
                                                                          QStringList* warnings = nullptr) const;

 private:
  QString dir_;
  QVariantList presets_;
  QVariantList favorite_hairs_;
  QVariantList head_eye_combos_;
  QVariantList facial_configs_;
  QVariantList imported_resources_;
};

}  // namespace sns::app
