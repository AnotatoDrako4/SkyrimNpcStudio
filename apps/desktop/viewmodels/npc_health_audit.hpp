#pragma once

#include <QSet>
#include <QString>
#include <QVariantMap>

#include "mo2_snapshot.hpp"
#include "sns/appearance/control_map.hpp"
#include "sns/appearance/npc_audit.hpp"

// NPC Health & Beauty Center: gathers the facts the appearance audit needs for one NPC from its getNpc
// details and the immutable MO2 snapshot, and runs the audit. Thread-safe (reads the snapshot only), so
// the profile scan runs it on a pool thread next to the control map.
namespace sns::app {

struct NpcHealthEntry {
  appearance::AuditSubject subject;
  appearance::NpcAudit audit;
  bool facegen_parsed = false;  // the effective FaceGen was read and parsed (shape and texture checks ran)
  int facegen_hair_vertices = 0;  // largest hair shape of the effective FaceGen (wig repair: full hair or hairline)
};

// `inputs` / `report`: the control map of the same details (build_control_inputs + analyze_appearance_control).
// Reads the effective FaceGen once: its shapes tell whether the record's head parts are in the head the game
// draws, and its hair shapes name the hair textures the game loads. May throw on unreadable archives; the
// caller turns that into one scan error.
NpcHealthEntry audit_npc_health(const SnapshotPtr& snapshot, const QSet<QString>& races_with_facegen, const QVariantMap& details,
                                const appearance::ControlInputs& inputs, const appearance::ControlReport& report);

// One row of the health scan (also one entry of NPC_APPEARANCE_FINDINGS.json).
QVariantMap npc_health_row(const NpcHealthEntry& entry, const QVariantMap& details, const appearance::ControlInputs& inputs);

}  // namespace sns::app
