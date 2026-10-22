#include "sns/appearance/skin_match.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sns::appearance {
namespace {

double to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double lab_f(double t) {
  constexpr double kEps = 216.0 / 24389.0;
  constexpr double kKappa = 24389.0 / 27.0;
  return t > kEps ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0;
}

std::array<double, 3> to_lab(const std::array<float, 3>& rgb) {
  const double r = to_linear(std::clamp<double>(rgb[0], 0.0, 1.0));
  const double g = to_linear(std::clamp<double>(rgb[1], 0.0, 1.0));
  const double b = to_linear(std::clamp<double>(rgb[2], 0.0, 1.0));
  // sRGB D65 -> XYZ
  const double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
  const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
  const double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
  const double fx = lab_f(x / 0.95047), fy = lab_f(y / 1.0), fz = lab_f(z / 1.08883);
  return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

bool is_body_part(const std::string& role) { return role == "body" || role == "hands" || role == "feet"; }

}  // namespace

std::string_view to_string(SkinVerdict verdict) {
  switch (verdict) {
    case SkinVerdict::Consistent: return "CONSISTENT";
    case SkinVerdict::Noticeable: return "NOTICEABLE";
    case SkinVerdict::LikelySeam: return "LIKELY_SEAM";
    case SkinVerdict::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

std::string skin_role_of_slots(std::uint32_t slot_mask) {
  // Biped slots are bits from 30: 32 body = bit 2, 33 hands = bit 3, 34 forearms = bit 4, 37 feet = bit 7, 30 head = bit 0.
  if (slot_mask & (1u << 2)) return "body";
  if (slot_mask & ((1u << 3) | (1u << 4))) return "hands";
  if (slot_mask & (1u << 7)) return "feet";
  if (slot_mask & 1u) return "head";
  return "other";
}

double delta_e76(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  const auto la = to_lab(a), lb = to_lab(b);
  return std::sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) + (la[2] - lb[2]) * (la[2] - lb[2]));
}

SkinMatch evaluate_skin_match(const std::vector<SkinSample>& samples) {
  SkinMatch m;
  m.caveats = {"ESTIMATE", "UNVERIFIED_IN_SKYRIM"};
  const SkinSample* head = nullptr;
  for (const auto& s : samples) {
    if (s.role == "head") { head = &s; break; }
  }
  if (!head) {
    m.codes.push_back("HeadNotMeasured");
    m.message = "No se pudo medir la piel de la cabeza (falta la textura o el FaceGen): no hay con qué compararla.";
    return m;
  }
  for (const auto& s : samples) {
    if (!is_body_part(s.role)) continue;
    SkinPairDelta d;
    d.role = s.role;
    d.label = s.label;
    d.delta_e = delta_e76(head->rgb, s.rgb);
    if (d.delta_e > m.worst || m.pairs.empty()) {
      m.worst = d.delta_e;
      m.worst_role = s.role;
    }
    m.pairs.push_back(std::move(d));
  }
  if (m.pairs.empty()) {
    m.codes.push_back("BodyNotMeasured");
    m.message = "No se pudo medir la piel del cuerpo, las manos ni los pies (cubiertos por ropa o sin textura): no hay con qué comparar la cabeza.";
    return m;
  }
  char text[260];
  if (m.worst <= kConsistentMax) {
    m.verdict = SkinVerdict::Consistent;
    std::snprintf(text, sizeof(text), "La piel de la cabeza y la del cuerpo tienen casi el mismo color (ΔE máximo %.1f, en %s): no se espera costura en el cuello.",
                  m.worst, m.worst_role.c_str());
  } else if (m.worst <= kNoticeableMax) {
    m.verdict = SkinVerdict::Noticeable;
    m.codes.push_back("SeamPossible");
    std::snprintf(text, sizeof(text), "La piel de la cabeza difiere de forma apreciable de la de %s (ΔE %.1f): con buena luz puede notarse una costura en el cuello.",
                  m.worst_role.c_str(), m.worst);
  } else {
    m.verdict = SkinVerdict::LikelySeam;
    m.codes.push_back("SeamLikely");
    std::snprintf(text, sizeof(text), "La piel de la cabeza difiere claramente de la de %s (ΔE %.1f): es probable que se vea una costura en el cuello.",
                  m.worst_role.c_str(), m.worst);
  }
  m.message = text;
  return m;
}

}  // namespace sns::appearance
