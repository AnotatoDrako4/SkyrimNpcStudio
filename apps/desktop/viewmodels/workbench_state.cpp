#include "workbench_state.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace sns::app {

namespace {

constexpr int kSaveDelayMs = 400;
constexpr std::array<int, 7> kZoomSteps = {85, 90, 100, 110, 125, 135, 150};
constexpr int kMaxLayoutNameLength = 40;

LayoutValues defaults_for(const QString& workspace) {
  LayoutValues v;
  if (workspace == "creator") { v.left_width = 340; v.right_width = 420; }
  else if (workspace == "outfit") { v.left_width = 380; v.right_width = 380; }
  else if (workspace == "forge") { v.left_width = 240; v.right_width = 340; }
  return v;
}

struct BuiltinLayout {
  const char* key;
  const char* name;
};
constexpr std::array<BuiltinLayout, 6> kBuiltin = {{
    {"creator", "Character Creator"},
    {"face", "Edición facial"},
    {"wardrobe", "Vestuario"},
    {"explore", "Exploración de NPC"},
    {"viewer", "Visor maximizado"},
    {"compact", "Diseño compacto"},
}};

// What a built-in layout does to a workspace, starting from that workspace's own defaults.
std::optional<LayoutValues> builtin_values(const QString& key, const LayoutValues& base) {
  LayoutValues v = base;
  if (key == "creator") return v;
  if (key == "face") { v.left_visible = false; v.right_visible = true; v.right_width = 480; return v; }
  if (key == "wardrobe") { v.left_visible = true; v.left_width = 420; v.right_visible = true; v.right_width = 380; return v; }
  if (key == "explore") { v.left_visible = true; v.left_width = 480; v.right_visible = false; return v; }
  if (key == "viewer") { v.maximized = true; return v; }
  if (key == "compact") { v.left_width = 240; v.right_width = 300; v.bottom_visible = false; return v; }
  return std::nullopt;
}

QJsonObject to_json(const LayoutValues& v) {
  return {{"left", v.left_visible},       {"right", v.right_visible},      {"bottom", v.bottom_visible},
          {"maximized", v.maximized},     {"leftWidth", v.left_width},     {"rightWidth", v.right_width},
          {"bottomHeight", v.bottom_height}};
}

LayoutValues from_json(const QJsonObject& o, const LayoutValues& base) {
  LayoutValues v = base;
  v.left_visible = o.value("left").toBool(base.left_visible);
  v.right_visible = o.value("right").toBool(base.right_visible);
  v.bottom_visible = o.value("bottom").toBool(base.bottom_visible);
  v.maximized = o.value("maximized").toBool(base.maximized);
  v.left_width = o.value("leftWidth").toInt(base.left_width);
  v.right_width = o.value("rightWidth").toInt(base.right_width);
  v.bottom_height = o.value("bottomHeight").toInt(base.bottom_height);
  return v;
}

QString clean_name(const QString& name) { return name.simplified().left(kMaxLayoutNameLength); }

}  // namespace

// ------------------------------------------------------------------------------ WorkspaceLayout

WorkspaceLayout::WorkspaceLayout(QString id, LayoutValues defaults, QObject* parent)
    : QObject(parent), id_(std::move(id)), defaults_(sanitized(defaults)), v_(defaults_) {}

LayoutValues WorkspaceLayout::sanitized(LayoutValues v) {
  v.left_width = std::clamp(v.left_width, kMinLeftWidth, kMaxLeftWidth);
  v.right_width = std::clamp(v.right_width, kMinRightWidth, kMaxRightWidth);
  v.bottom_height = std::clamp(v.bottom_height, kMinBottomHeight, kMaxBottomHeight);
  return v;
}

#define SNS_LAYOUT_SETTER(setter, field, signal, clamp_expr) \
  void WorkspaceLayout::setter(decltype(LayoutValues::field) value) { \
    value = clamp_expr; \
    if (v_.field == value) return; \
    v_.field = value; \
    emit signal(); \
    emit changed(); \
  }
SNS_LAYOUT_SETTER(set_left_visible, left_visible, leftVisibleChanged, value)
SNS_LAYOUT_SETTER(set_right_visible, right_visible, rightVisibleChanged, value)
SNS_LAYOUT_SETTER(set_bottom_visible, bottom_visible, bottomVisibleChanged, value)
SNS_LAYOUT_SETTER(set_maximized, maximized, maximizedChanged, value)
SNS_LAYOUT_SETTER(set_left_width, left_width, leftWidthChanged, std::clamp(value, kMinLeftWidth, kMaxLeftWidth))
SNS_LAYOUT_SETTER(set_right_width, right_width, rightWidthChanged, std::clamp(value, kMinRightWidth, kMaxRightWidth))
SNS_LAYOUT_SETTER(set_bottom_height, bottom_height, bottomHeightChanged, std::clamp(value, kMinBottomHeight, kMaxBottomHeight))
#undef SNS_LAYOUT_SETTER

void WorkspaceLayout::set_values(const LayoutValues& values) {
  const auto v = sanitized(values);
  set_left_visible(v.left_visible);
  set_right_visible(v.right_visible);
  set_bottom_visible(v.bottom_visible);
  set_maximized(v.maximized);
  set_left_width(v.left_width);
  set_right_width(v.right_width);
  set_bottom_height(v.bottom_height);
}

// ------------------------------------------------------------------------------ WorkbenchState

WorkbenchState::WorkbenchState(QObject* parent) : QObject(parent), settings_(std::make_unique<QSettings>()) { init(); }

WorkbenchState::WorkbenchState(const QString& ini_path, QObject* parent)
    : QObject(parent), settings_(std::make_unique<QSettings>(ini_path, QSettings::IniFormat)) {
  init();
}

WorkbenchState::~WorkbenchState() { flush(); }

void WorkbenchState::init() {
  save_timer_.setSingleShot(true);
  save_timer_.setInterval(kSaveDelayMs);
  connect(&save_timer_, &QTimer::timeout, this, &WorkbenchState::save);
  for (const auto& zone : kZones) zone_fonts_.insert(zone, 100);
  for (const auto& id : kWorkspaces) {
    auto* layout = new WorkspaceLayout(id, defaults_for(id), this);
    QQmlEngine::setObjectOwnership(layout, QQmlEngine::CppOwnership);  // handed to QML by layout()
    connect(layout, &WorkspaceLayout::changed, this, &WorkbenchState::schedule_save);
    layouts_.insert(id, layout);
  }
  load();
}

void WorkbenchState::load() {
  loading_ = true;
  settings_->beginGroup("workbench");
  ui_scale_ = std::clamp(settings_->value("uiScale", 100).toInt(), kMinUiScale, kMaxUiScale);
  font_scale_ = std::clamp(settings_->value("fontScale", 100).toInt(), kMinFontScale, kMaxFontScale);
  density_ = std::clamp(settings_->value("density", Normal).toInt(), int(Compact), int(Comfortable));
  font_family_ = settings_->value("fontFamily").toString().simplified();
  workspace_ = std::clamp(settings_->value("workspace", 0).toInt(), 0, int(kWorkspaces.size()) - 1);
  for (const auto& zone : kZones)
    zone_fonts_[zone] = std::clamp(settings_->value("zone/" + zone, 100).toInt(), kMinZoneFont, kMaxZoneFont);
  for (auto* layout : std::as_const(layouts_)) {
    const auto base = layout->defaults();
    const QString prefix = "layout/" + layout->id() + "/";
    LayoutValues v = base;
    v.left_visible = settings_->value(prefix + "left", base.left_visible).toBool();
    v.right_visible = settings_->value(prefix + "right", base.right_visible).toBool();
    v.bottom_visible = settings_->value(prefix + "bottom", base.bottom_visible).toBool();
    v.maximized = settings_->value(prefix + "maximized", base.maximized).toBool();
    v.left_width = settings_->value(prefix + "leftWidth", base.left_width).toInt();
    v.right_width = settings_->value(prefix + "rightWidth", base.right_width).toInt();
    v.bottom_height = settings_->value(prefix + "bottomHeight", base.bottom_height).toInt();
    layout->set_values(v);
  }
  settings_->endGroup();
  loading_ = false;
}

void WorkbenchState::save() {
  save_timer_.stop();
  settings_->beginGroup("workbench");
  settings_->setValue("version", 1);
  settings_->setValue("uiScale", ui_scale_);
  settings_->setValue("fontScale", font_scale_);
  settings_->setValue("density", density_);
  settings_->setValue("fontFamily", font_family_);
  settings_->setValue("workspace", workspace_);
  for (const auto& zone : kZones) settings_->setValue("zone/" + zone, zone_fonts_.value(zone, 100));
  for (const auto* layout : std::as_const(layouts_)) {
    const auto& v = layout->values();
    const QString prefix = "layout/" + layout->id() + "/";
    settings_->setValue(prefix + "left", v.left_visible);
    settings_->setValue(prefix + "right", v.right_visible);
    settings_->setValue(prefix + "bottom", v.bottom_visible);
    settings_->setValue(prefix + "maximized", v.maximized);
    settings_->setValue(prefix + "leftWidth", v.left_width);
    settings_->setValue(prefix + "rightWidth", v.right_width);
    settings_->setValue(prefix + "bottomHeight", v.bottom_height);
  }
  settings_->endGroup();
  settings_->sync();
}

void WorkbenchState::schedule_save() {
  if (!loading_) save_timer_.start();
}

void WorkbenchState::flush() {
  if (save_timer_.isActive()) save();
}

QVariantMap WorkbenchState::zone_fonts() const {
  QVariantMap map;
  for (auto it = zone_fonts_.cbegin(); it != zone_fonts_.cend(); ++it) map.insert(it.key(), it.value());
  return map;
}

int WorkbenchState::zone_font(const QString& zone) const { return zone_fonts_.value(zone, 100); }

bool WorkbenchState::appearance_at_defaults() const {
  return ui_scale_ == 100 && font_scale_ == 100 && density_ == Normal && font_family_.isEmpty() &&
         std::all_of(zone_fonts_.cbegin(), zone_fonts_.cend(), [](int v) { return v == 100; });
}

void WorkbenchState::set_ui_scale(int percent) {
  percent = std::clamp(percent, kMinUiScale, kMaxUiScale);
  if (percent == ui_scale_) return;
  ui_scale_ = percent;
  emit uiScaleChanged();
  emit appearanceChanged();
  schedule_save();
}

void WorkbenchState::set_font_scale(int percent) {
  percent = std::clamp(percent, kMinFontScale, kMaxFontScale);
  if (percent == font_scale_) return;
  font_scale_ = percent;
  emit fontScaleChanged();
  emit appearanceChanged();
  schedule_save();
}

void WorkbenchState::set_density(int density) {
  density = std::clamp(density, int(Compact), int(Comfortable));
  if (density == density_) return;
  density_ = density;
  emit densityChanged();
  emit appearanceChanged();
  schedule_save();
}

void WorkbenchState::set_font_family(const QString& family) {
  const auto clean = family.simplified();
  if (clean == font_family_) return;
  font_family_ = clean;
  emit fontFamilyChanged();
  emit appearanceChanged();
  schedule_save();
}

void WorkbenchState::set_workspace(int index) {
  index = std::clamp(index, 0, int(kWorkspaces.size()) - 1);
  if (index == workspace_) return;
  workspace_ = index;
  emit workspaceChanged();
  schedule_save();
}

void WorkbenchState::setZoneFont(const QString& zone, int percent) {
  if (!zone_fonts_.contains(zone)) return;
  percent = std::clamp(percent, kMinZoneFont, kMaxZoneFont);
  if (zone_fonts_[zone] == percent) return;
  zone_fonts_[zone] = percent;
  emit zoneFontsChanged();
  emit appearanceChanged();
  schedule_save();
}

WorkspaceLayout* WorkbenchState::layout(const QString& workspace) const { return layouts_.value(workspace, nullptr); }

void WorkbenchState::zoomIn() {
  for (const int step : kZoomSteps)
    if (step > ui_scale_) { set_ui_scale(step); return; }
}

void WorkbenchState::zoomOut() {
  for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it)
    if (*it < ui_scale_) { set_ui_scale(*it); return; }
}

void WorkbenchState::zoomReset() { set_ui_scale(100); }

void WorkbenchState::resetAppearance() {
  set_ui_scale(100);
  set_font_scale(100);
  set_density(Normal);
  set_font_family({});
  for (const auto& zone : kZones) setZoneFont(zone, 100);
}

void WorkbenchState::resetLayouts() {
  for (auto* layout : std::as_const(layouts_)) layout->reset();
}

void WorkbenchState::resetAll() {
  resetAppearance();
  resetLayouts();
}

// ------------------------------------------------------------------------------ named layouts

QVariantList WorkbenchState::builtin_layouts() const {
  QVariantList list;
  for (const auto& b : kBuiltin) list.push_back(QVariantMap{{"key", QString::fromLatin1(b.key)}, {"name", QString::fromUtf8(b.name)}});
  return list;
}

bool WorkbenchState::applyBuiltinLayout(const QString& key, const QString& workspace) {
  auto* target = layout(workspace);
  if (!target) return false;
  const auto values = builtin_values(key, target->defaults());
  if (!values) return false;
  target->set_values(*values);
  return true;
}

QVariantMap WorkbenchState::read_user_layouts() const {
  const auto doc = QJsonDocument::fromJson(settings_->value("workbench/userLayouts").toString().toUtf8());
  return doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
}

void WorkbenchState::write_user_layouts(const QVariantMap& layouts) {
  settings_->setValue("workbench/userLayouts", QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(layouts)).toJson(QJsonDocument::Compact)));
  settings_->sync();
  emit savedLayoutsChanged();
}

QStringList WorkbenchState::saved_layouts() const {
  auto names = read_user_layouts().keys();
  names.sort(Qt::CaseInsensitive);
  return names;
}

bool WorkbenchState::saveLayout(const QString& name) {
  const auto clean = clean_name(name);
  if (clean.isEmpty()) return false;
  for (const auto& b : kBuiltin)
    if (clean.compare(QString::fromUtf8(b.name), Qt::CaseInsensitive) == 0) return false;  // a built-in name is not overwritten
  QJsonObject all;
  for (const auto* layout : std::as_const(layouts_)) all.insert(layout->id(), to_json(layout->values()));
  auto layouts = read_user_layouts();
  // Same name in another case replaces it instead of listing two.
  for (const auto& existing : layouts.keys())
    if (existing.compare(clean, Qt::CaseInsensitive) == 0) layouts.remove(existing);
  layouts.insert(clean, all.toVariantMap());
  write_user_layouts(layouts);
  return true;
}

bool WorkbenchState::applyLayout(const QString& name) {
  const auto layouts = read_user_layouts();
  const auto it = layouts.constFind(clean_name(name));
  if (it == layouts.constEnd()) return false;
  const auto all = QJsonObject::fromVariantMap(it->toMap());
  for (auto* layout : std::as_const(layouts_)) {
    const auto entry = all.value(layout->id());
    if (entry.isObject()) layout->set_values(from_json(entry.toObject(), layout->defaults()));
  }
  return true;
}

bool WorkbenchState::deleteLayout(const QString& name) {
  auto layouts = read_user_layouts();
  if (layouts.remove(clean_name(name)) == 0) return false;
  write_user_layouts(layouts);
  return true;
}

}  // namespace sns::app
