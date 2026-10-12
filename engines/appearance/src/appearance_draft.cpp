#include "sns/appearance/appearance_draft.hpp"

namespace sns::appearance {

namespace {

class ApplyDonorCommand final : public projects::Command {
 public:
  ApplyDonorCommand(AppearanceDraft& draft, projects::FaceConfig donor, std::string desc)
      : draft_(draft), new_face_(std::move(donor)), description_(std::move(desc)) {
    old_face_ = draft_.face().config;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().config = new_face_;
    draft_.face().active = true;
  }

  void revert() override {
    draft_.face().config = old_face_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override { return description_; }

 private:
  AppearanceDraft& draft_;
  projects::FaceConfig old_face_;
  bool old_active_{false};
  projects::FaceConfig new_face_;
  std::string description_;
};

class SetSliderCommand final : public projects::Command {
 public:
  SetSliderCommand(AppearanceDraft& draft, std::string name, float value)
      : draft_(draft), name_(std::move(name)), new_val_(value) {
    had_old_val_ = draft_.face().config.morph_sliders.contains(name_);
    old_val_ = draft_.face().slider_value(name_, 0.0f);
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().set_slider(name_, new_val_);
  }

  void revert() override {
    if (had_old_val_) {
      draft_.face().config.morph_sliders[name_] = old_val_;
    } else {
      draft_.face().config.morph_sliders.erase(name_);
    }
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Ajustar slider facial: " + name_;
  }

 private:
  AppearanceDraft& draft_;
  std::string name_;
  float old_val_{0.0f};
  bool had_old_val_{false};
  float new_val_{0.0f};
  bool old_active_{false};
};

class SetHairColorCommand final : public projects::Command {
 public:
  SetHairColorCommand(AppearanceDraft& draft, std::optional<projects::ColorRgb> color)
      : draft_(draft), new_color_(color) {
    old_color_ = draft_.face().config.hair_color;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().config.hair_color = new_color_;
    draft_.face().active = true;
  }

  void revert() override {
    draft_.face().config.hair_color = old_color_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Cambiar color de cabello";
  }

 private:
  AppearanceDraft& draft_;
  std::optional<projects::ColorRgb> old_color_;
  std::optional<projects::ColorRgb> new_color_;
  bool old_active_{false};
};

class SetSkinToneCommand final : public projects::Command {
 public:
  SetSkinToneCommand(AppearanceDraft& draft, std::optional<projects::ColorRgb> color)
      : draft_(draft), new_color_(color) {
    old_color_ = draft_.face().config.skin_tone;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().config.skin_tone = new_color_;
    draft_.face().active = true;
  }

  void revert() override {
    draft_.face().config.skin_tone = old_color_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Cambiar tono de piel";
  }

 private:
  AppearanceDraft& draft_;
  std::optional<projects::ColorRgb> old_color_;
  std::optional<projects::ColorRgb> new_color_;
  bool old_active_{false};
};

class SetHeadPartCommand final : public projects::Command {
 public:
  SetHeadPartCommand(AppearanceDraft& draft, projects::HeadPartRef part)
      : draft_(draft), new_part_(std::move(part)) {
    old_parts_ = draft_.face().config.head_parts;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().set_head_part(new_part_);
  }

  void revert() override {
    draft_.face().config.head_parts = old_parts_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Cambiar " + new_part_.type + " (" + (new_part_.editor_id.empty() ? new_part_.form_key : new_part_.editor_id) + ")";
  }

 private:
  AppearanceDraft& draft_;
  projects::HeadPartRef new_part_;
  std::vector<projects::HeadPartRef> old_parts_;
  bool old_active_{false};
};

class RemoveHeadPartCommand final : public projects::Command {
 public:
  RemoveHeadPartCommand(AppearanceDraft& draft, std::string type)
      : draft_(draft), type_(std::move(type)) {
    old_parts_ = draft_.face().config.head_parts;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().remove_head_part(type_);
    draft_.face().active = true;
  }

  void revert() override {
    draft_.face().config.head_parts = old_parts_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Quitar " + type_;
  }

 private:
  AppearanceDraft& draft_;
  std::string type_;
  std::vector<projects::HeadPartRef> old_parts_;
  bool old_active_{false};
};

class SetBodyDimensionsCommand final : public projects::Command {
 public:
  SetBodyDimensionsCommand(AppearanceDraft& draft, float weight, float height)
      : draft_(draft), new_weight_(weight), new_height_(height) {
    old_weight_ = draft_.face().config.weight;
    old_height_ = draft_.face().config.height;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().config.weight = new_weight_;
    draft_.face().config.height = new_height_;
    draft_.face().active = true;
  }

  void revert() override {
    draft_.face().config.weight = old_weight_;
    draft_.face().config.height = old_height_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Modificar peso y estatura";
  }

 private:
  AppearanceDraft& draft_;
  float old_weight_{100.0f};
  float old_height_{1.0f};
  float new_weight_{100.0f};
  float new_height_{1.0f};
  bool old_active_{false};
};

class ResetFaceCommand final : public projects::Command {
 public:
  explicit ResetFaceCommand(AppearanceDraft& draft)
      : draft_(draft) {
    old_face_ = draft_.face().config;
    old_active_ = draft_.face().active;
  }

  void apply() override {
    draft_.face().config = draft_.initial_face();
    draft_.face().active = false;
  }

  void revert() override {
    draft_.face().config = old_face_;
    draft_.face().active = old_active_;
  }

  [[nodiscard]] std::string description() const override {
    return "Restablecer rostro al inicial";
  }

 private:
  AppearanceDraft& draft_;
  projects::FaceConfig old_face_;
  bool old_active_{false};
};

}  // namespace

std::unique_ptr<projects::Command> make_apply_donor_face(AppearanceDraft& draft, projects::FaceConfig donor, std::string description) {
  return std::make_unique<ApplyDonorCommand>(draft, std::move(donor), std::move(description));
}

std::unique_ptr<projects::Command> make_set_face_slider(AppearanceDraft& draft, std::string slider_name, float value) {
  return std::make_unique<SetSliderCommand>(draft, std::move(slider_name), value);
}

std::unique_ptr<projects::Command> make_set_hair_color(AppearanceDraft& draft, std::optional<projects::ColorRgb> color) {
  return std::make_unique<SetHairColorCommand>(draft, color);
}

std::unique_ptr<projects::Command> make_set_skin_tone(AppearanceDraft& draft, std::optional<projects::ColorRgb> color) {
  return std::make_unique<SetSkinToneCommand>(draft, color);
}

std::unique_ptr<projects::Command> make_set_head_part(AppearanceDraft& draft, projects::HeadPartRef part) {
  return std::make_unique<SetHeadPartCommand>(draft, std::move(part));
}

std::unique_ptr<projects::Command> make_remove_head_part(AppearanceDraft& draft, std::string type) {
  return std::make_unique<RemoveHeadPartCommand>(draft, std::move(type));
}

std::unique_ptr<projects::Command> make_set_body_dimensions(AppearanceDraft& draft, float weight, float height) {
  return std::make_unique<SetBodyDimensionsCommand>(draft, weight, height);
}

std::unique_ptr<projects::Command> make_reset_face(AppearanceDraft& draft) {
  return std::make_unique<ResetFaceCommand>(draft);
}

}  // namespace sns::appearance
