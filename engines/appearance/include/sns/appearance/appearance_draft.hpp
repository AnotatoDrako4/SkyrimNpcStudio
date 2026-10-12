#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sns/outfit/outfit_draft.hpp"
#include "sns/projects/command_stack.hpp"
#include "sns/projects/project.hpp"

namespace sns::appearance {

struct FaceDraft {
  projects::FaceConfig config;
  bool active = false;

  [[nodiscard]] float slider_value(const std::string& name, float default_val = 0.0f) const {
    const auto it = config.morph_sliders.find(name);
    return it != config.morph_sliders.end() ? it->second : default_val;
  }

  void set_slider(std::string name, float val) {
    config.morph_sliders[std::move(name)] = val;
    active = true;
  }

  void set_head_part(projects::HeadPartRef part) {
    for (auto& p : config.head_parts) {
      if (p.type == part.type) {
        p = std::move(part);
        active = true;
        return;
      }
    }
    config.head_parts.push_back(std::move(part));
    active = true;
  }

  void remove_head_part(const std::string& type) {
    std::erase_if(config.head_parts, [&](const auto& p) { return p.type == type; });
  }

  void clear() {
    config = {};
    active = false;
  }
};

class AppearanceDraft {
 public:
  AppearanceDraft() = default;

  [[nodiscard]] const projects::NpcTarget& target() const noexcept { return target_; }
  void set_target(projects::NpcTarget target) { target_ = std::move(target); }

  [[nodiscard]] const outfit::OutfitDraft& outfit() const noexcept { return outfit_; }
  [[nodiscard]] outfit::OutfitDraft& outfit() noexcept { return outfit_; }

  [[nodiscard]] const FaceDraft& face() const noexcept { return face_; }
  [[nodiscard]] FaceDraft& face() noexcept { return face_; }

  [[nodiscard]] const projects::FaceConfig& initial_face() const noexcept { return initial_face_; }
  void set_initial_face(projects::FaceConfig face) {
    initial_face_ = face;
    face_.config = std::move(face);
    face_.active = false;
  }

  [[nodiscard]] const std::vector<outfit::Piece>& initial_outfit() const noexcept { return initial_outfit_; }
  void set_initial_outfit(std::vector<outfit::Piece> pieces) {
    initial_outfit_ = pieces;
    outfit_.assign(std::move(pieces));
  }

  [[nodiscard]] bool is_face_dirty() const noexcept { return face_.active; }
  [[nodiscard]] bool is_outfit_dirty() const noexcept { return outfit_.pieces() != initial_outfit_; }
  [[nodiscard]] bool is_dirty() const noexcept { return is_face_dirty() || is_outfit_dirty(); }

  [[nodiscard]] projects::NpcEdit to_npc_edit() const {
    projects::NpcEdit edit;
    edit.target = target_;
    if (!outfit_.pieces().empty()) {
      edit.outfit = projects::OutfitConfig{
          .outfit_editor_id = "SNS_" + (target_.editor_id.empty() ? "Npc" : target_.editor_id) + "Outfit",
          .pieces = outfit_.pieces(),
          .also_sleeping = false,
      };
    }
    if (face_.active) {
      edit.face = face_.config;
    }
    return edit;
  }

  void load_npc_edit(const projects::NpcEdit& edit) {
    target_ = edit.target;
    if (edit.outfit) {
      outfit_.assign(edit.outfit->pieces);
    } else {
      outfit_.assign({});
    }
    if (edit.face) {
      face_.config = *edit.face;
      face_.active = true;
    } else {
      face_.clear();
    }
  }

  void reset_to_initial() {
    outfit_.assign(initial_outfit_);
    face_.config = initial_face_;
    face_.active = false;
  }

 private:
  projects::NpcTarget target_;
  outfit::OutfitDraft outfit_;
  FaceDraft face_;
  projects::FaceConfig initial_face_;
  std::vector<outfit::Piece> initial_outfit_;
};

// Command factories for undo/redo
std::unique_ptr<projects::Command> make_apply_donor_face(AppearanceDraft& draft, projects::FaceConfig donor, std::string description);
std::unique_ptr<projects::Command> make_set_face_slider(AppearanceDraft& draft, std::string slider_name, float value);
std::unique_ptr<projects::Command> make_set_hair_color(AppearanceDraft& draft, std::optional<projects::ColorRgb> color);
std::unique_ptr<projects::Command> make_set_skin_tone(AppearanceDraft& draft, std::optional<projects::ColorRgb> color);
std::unique_ptr<projects::Command> make_set_head_part(AppearanceDraft& draft, projects::HeadPartRef part);
std::unique_ptr<projects::Command> make_remove_head_part(AppearanceDraft& draft, std::string type);
std::unique_ptr<projects::Command> make_set_body_dimensions(AppearanceDraft& draft, float weight, float height);
std::unique_ptr<projects::Command> make_reset_face(AppearanceDraft& draft);

}  // namespace sns::appearance
