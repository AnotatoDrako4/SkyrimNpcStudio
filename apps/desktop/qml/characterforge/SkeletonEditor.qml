import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var inspection: forge.scene.inspection || ({})
    readonly property var skins: inspection.skins || []
    readonly property var skyrimConv: forge.skyrimConversion || ({ state: "none" })
    readonly property var skMap: skyrimConv.skeletonMapping || ({})
    readonly property var mappedBones: skMap.bones || []
    readonly property var skCounts: skMap.counts || ({})
    readonly property int declaredJoints: (inspection.declared || ({})).joints || 0

    // Prefer the authoritative skeletonMapping from SkyrimConversion (when analyzed in "skinned" profile);
    // otherwise fall back to the glTF skin joints inspected by ForgeSceneViewModel.
    readonly property var skeletonData: {
        const out = [];
        if (mappedBones.length > 0) {
            out.push({
                name: qsTr("Armature «%1» vs %2 (%3 huesos)")
                    .arg(skMap.armature || qsTr("sin nombre"))
                    .arg((skMap.reference || ({})).label || "skeleton.nif")
                    .arg(mappedBones.length),
                depth: 0,
                status: skMap.allWeightedVerified ? "verified" : "warning",
                diagnostic: qsTr("Verificados: %1 · Sugeridos: %2 · No reconocidos: %3 · Incompatibles: %4")
                    .arg(skCounts.verified || 0).arg(skCounts.suggested || 0)
                    .arg(skCounts.unrecognized || 0).arg(skCounts.incompatible || 0)
            });
            for (let i = 0; i < mappedBones.length; ++i) {
                const b = mappedBones[i];
                out.push({
                    name: b.name + (b.weighted ? qsTr(" [con peso]") : "") + " — " + (b.status || ""),
                    depth: 1,
                    status: b.status || "unrecognized",
                    diagnostic: (b.detail || "")
                        + (b.reference ? qsTr(" · Ref: %1").arg(b.reference) : "")
                        + (b.translationError !== undefined ? qsTr(" · Δpos=%1 u, Δrot=%2°").arg(Number(b.translationError).toFixed(4)).arg(Number(b.rotationErrorDeg || 0).toFixed(2)) : "")
                });
            }
            return out;
        }
        for (let s = 0; s < skins.length; ++s) {
            const sk = skins[s];
            const joints = sk.joints || [];
            out.push({
                name: qsTr("Armature: %1 (%2 huesos)").arg(sk.name || qsTr("sin nombre")).arg(joints.length),
                depth: 0,
                status: "info",
                diagnostic: qsTr("Armature importada con %1 articulaciones. Pulsa «Mapear contra skeleton.nif de Skyrim SE» para validar transformaciones bind y pesos.")
                    .arg(joints.length)
            });
            for (let j = 0; j < joints.length; ++j) {
                const bName = String(joints[j]);
                const skyrimMatch = bName.indexOf("NPC ") === 0 || bName.indexOf("CME ") === 0 || bName.indexOf("Weapon") === 0 || bName.indexOf("Shield") === 0;
                out.push({
                    name: bName,
                    depth: 1,
                    status: skyrimMatch ? "verified" : "unrecognized",
                    diagnostic: skyrimMatch
                        ? qsTr("Nombre compatible con la convención de Skyrim SE («%1»). Ejecuta el análisis SkinnedMesh para comprobar bind pose.").arg(bName)
                        : qsTr("Hueso personalizado («%1»): no coincide con nombres canónicos NPC de Skyrim SE.").arg(bName)
                });
            }
        }
        return out;
    }

    Text {
        text: qsTr("Esqueleto y rigging")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: root.mappedBones.length > 0
            ? qsTr("Mapeo real contra %1 (%2): %3 verificados, %4 sugeridos, %5 no reconocidos, %6 incompatibles.")
                .arg((root.skMap.reference || ({})).label || "skeleton.nif")
                .arg(String((root.skMap.reference || ({})).sha256 || "").substring(0, 12))
                .arg(root.skCounts.verified || 0).arg(root.skCounts.suggested || 0)
                .arg(root.skCounts.unrecognized || 0).arg(root.skCounts.incompatible || 0)
            : root.skins.length > 0
            ? qsTr("Se detectaron %1 armature(s) y %2 hueso(s) en la vista previa GLB. El renderizador 3D (Qt 6.11 RuntimeLoader) no expone transformaciones de Joint en vivo; aquí puedes inspeccionar los huesos y validar el rig contra skeleton.nif.")
                .arg(root.skins.length).arg(root.declaredJoints)
            : qsTr("El modelo actual no declara armatures ni huesos en la vista previa GLB (o aún no ha sido analizado).")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Mapear y analizar esqueleto contra Skyrim SE (SkinnedMesh)")
        enabled: root.forge.hasProject && root.forge.projectPath.length > 0 && !root.forge.busy
                 && !!(root.forge.workspace.model && root.forge.workspace.model.path)
        onClicked: {
            if (!root.forge.workspace.blender || !root.forge.workspace.blender.executable)
                root.forge.detectBlender();
            root.forge.setSkyrimProfile("skinned");
            root.forge.analyzeSkyrimCompatibility();
        }
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Activa el perfil SkinnedMesh y compara cada hueso y su bind pose contra el skeleton.nif de referencia.")
    }
    Text {
        visible: root.skeletonData.length === 0
        text: qsTr("Sin huesos inspeccionados")
        color: Theme.textMuted
    }
    Repeater {
        model: root.skeletonData
        ItemDelegate {
            required property var modelData
            Layout.fillWidth: true
            text: (modelData.depth > 0
                    ? (modelData.status === "verified" ? "✓ " : modelData.status === "suggested" ? "≈ " : modelData.status === "incompatible" ? "✗ " : "• ")
                    : "▾ ") + modelData.name
            leftPadding: 8 + (modelData.depth || 0) * 14
            font.bold: (modelData.depth || 0) === 0
            onClicked: details.text = modelData.diagnostic || qsTr("Sin diagnóstico")
        }
    }
    Text {
        id: details
        Layout.fillWidth: true
        text: root.skeletonData.length > 0
            ? qsTr("Haz clic en un hueso de la lista para ver su diagnóstico de mapeo y error de transformación frente al esqueleto de Skyrim SE.")
            : qsTr("Compatibilidad Skyrim pendiente de validar recursos reales.")
        color: Theme.textDim
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Retargeting automático de huesos · requiere coincidencia exacta de bind pose")
        enabled: false
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Por política de integridad (skeleton_mapping.hpp), no se infiere anatomía ni se renombran huesos automáticamente sin validación humana.")
    }
}
