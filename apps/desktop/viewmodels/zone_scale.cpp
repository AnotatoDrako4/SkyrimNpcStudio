#include "zone_scale.hpp"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace sns::app {

namespace {
// The workbench allows 0.85 x 1.6 x 2.0 at most; anything outside this is a broken binding, not a choice.
constexpr qreal kMinFactor = 0.25, kMaxFactor = 6.0;
qreal clean(qreal v) { return std::isfinite(v) ? std::clamp(v, kMinFactor, kMaxFactor) : 1.0; }
}  // namespace

ZoneAttached::ZoneAttached(QObject* parent) : QQuickAttachedPropertyPropagator(parent) { initialize(); }

int ZoneAttached::px(int base, qreal scale) {
  return std::max(kMinPixelSize, static_cast<int>(std::lround(base * clean(scale))));
}

void ZoneAttached::apply_scale(qreal scale) {
  if (qFuzzyCompare(scale_, scale)) return;
  scale_ = scale;
  for (auto* child : attachedChildren())
    if (auto* zone = qobject_cast<ZoneAttached*>(child)) zone->inherit_scale(scale_);
  emit scaleChanged();
  emit unitChanged();
}

void ZoneAttached::apply_density(qreal density) {
  if (qFuzzyCompare(density_, density)) return;
  density_ = density;
  for (auto* child : attachedChildren())
    if (auto* zone = qobject_cast<ZoneAttached*>(child)) zone->inherit_density(density_);
  emit densityChanged();
  emit unitChanged();
}

void ZoneAttached::set_scale(qreal scale) {
  explicit_scale_ = true;
  apply_scale(clean(scale));
}

void ZoneAttached::inherit_scale(qreal scale) {
  if (!explicit_scale_) apply_scale(scale);
}

void ZoneAttached::reset_scale() {
  if (!explicit_scale_) return;
  explicit_scale_ = false;
  const auto* parent = qobject_cast<ZoneAttached*>(attachedParent());
  apply_scale(parent ? parent->scale() : 1.0);
}

void ZoneAttached::set_density(qreal density) {
  explicit_density_ = true;
  apply_density(clean(density));
}

void ZoneAttached::inherit_density(qreal density) {
  if (!explicit_density_) apply_density(density);
}

void ZoneAttached::reset_density() {
  if (!explicit_density_) return;
  explicit_density_ = false;
  const auto* parent = qobject_cast<ZoneAttached*>(attachedParent());
  apply_density(parent ? parent->density() : 1.0);
}

void ZoneAttached::attachedParentChange(QQuickAttachedPropertyPropagator* new_parent, QQuickAttachedPropertyPropagator*) {
  if (const auto* parent = qobject_cast<ZoneAttached*>(new_parent)) {
    inherit_scale(parent->scale());
    inherit_density(parent->density());
  }
}

}  // namespace sns::app
