import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    readonly property var modelInfo: forge.workspace.model || ({})

    signal saveAndConvertRequested()
    signal convertPreviewRequested(bool force)
    signal toolRequested(string tool)

    readonly property bool isExternalNonGltf: {
        if (!modelInfo.path || modelInfo.storage === "managed")
            return false;
        const fmt = String(modelInfo.format || "").toLowerCase();
        return fmt === "blend" || fmt === "fbx" || fmt === "obj";
    }

    // ForgeSceneViewModel computes the isolated part once per change and isolates with one signal.
    function isPartIsolated(partIndex) {
        return partIndex >= 0 && root.scene.isolatedPart === partIndex;
    }
    function isolatePart(partIndex) {
        root.scene.isolatePart(partIndex);
    }

    Text {
        text: qsTr("Importar personaje")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Seleccionar → Inspeccionar → Previsualizar → Adaptar")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Seleccionar archivo 3D…")
        enabled: root.forge.hasProject && !root.forge.busy
        onClicked: filePicker.open()
    }
    FileDialog {
        id: filePicker
        title: qsTr("Selecciona un modelo")
        nameFilters: ["Modelos 3D (*.glb *.gltf *.fbx *.obj *.blend)"]
        onAccepted: root.forge.selectModelFile(selectedFile.toString())
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Copiar modelo validado al proyecto")
        enabled: root.forge.hasProject && root.forge.projectPath.length > 0 && !root.forge.busy &&
                 root.modelInfo.storage !== "managed" && (root.modelInfo.format === "glb" || root.modelInfo.format === "gltf")
        onClicked: root.forge.copySelectedModelToProject()
    }
    // Automatic / manual BLEND, FBX, OBJ -> GLB conversion controls
    Button {
        Layout.fillWidth: true
        visible: root.isExternalNonGltf && root.forge.projectPath.length === 0
        highlighted: true
        enabled: !root.forge.busy
        text: qsTr("Guardar .sncf y convertir vista previa a GLB…")
        onClicked: root.saveAndConvertRequested()
    }
    Button {
        Layout.fillWidth: true
        visible: root.isExternalNonGltf && root.forge.projectPath.length > 0
        highlighted: true
        enabled: !root.forge.busy
        text: root.forge.blenderBusy
            ? qsTr("Convirtiendo a GLB en Blender…")
            : qsTr("Convertir a GLB y previsualizar")
        onClicked: root.convertPreviewRequested(true)
    }
    Button {
        Layout.fillWidth: true
        visible: root.forge.blenderBusy
        text: qsTr("Cancelar conversión de Blender")
        onClicked: root.forge.cancelOperation()
    }
    Text {
        Layout.fillWidth: true
        visible: root.modelInfo.storage === "managed"
        text: qsTr("Guardado en este proyecto · SHA-256: %1").arg(root.modelInfo.sha256 || "")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: !!root.modelInfo.sourcePath
        text: qsTr("Convertido desde: %1 (%2)").arg(root.modelInfo.sourcePath || "")
              .arg(root.modelInfo.blenderVersion ? "Blender " + root.modelInfo.blenderVersion : (root.modelInfo.sourceFormat || "BLEND"))
        color: Theme.accent
        font.pixelSize: Theme.small
        wrapMode: Text.WrapAnywhere
    }
    Button {
        Layout.fillWidth: true
        visible: !!root.modelInfo.sourcePath
        enabled: !root.forge.busy
        text: qsTr("Reconvertir desde archivo origen (.blend)")
        onClicked: root.convertPreviewRequested(true)
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Invalida la vista previa actual y vuelve a exportar el GLB desde el archivo original en Blender.")
    }
    Text {
        Layout.fillWidth: true
        text: root.modelInfo.name || qsTr("Sin archivo seleccionado")
        color: Theme.text
        wrapMode: Text.WordWrap
    }
    Text {
        text: root.modelInfo.format ? qsTr("Formato: %1 · %2 MiB").arg(root.modelInfo.format.toUpperCase()).arg((root.modelInfo.sizeBytes / 1048576).toFixed(2)) : ""
        color: Theme.textDim
    }
    Repeater {
        model: [
            {
                key: "meshes",
                label: qsTr("Mallas")
            },
            {
                key: "materials",
                label: qsTr("Materiales")
            },
            {
                key: "textures",
                label: qsTr("Texturas")
            },
            {
                key: "skins",
                label: qsTr("Skins / armatures")
            }
        ]
        Text {
            required property var modelData
            text: modelData.label + ": " + (root.modelInfo[modelData.key] === undefined ? qsTr("No inspeccionado") : root.modelInfo[modelData.key])
            color: Theme.textDim
        }
    }
    // Three levels, never mixed: declared (JSON), validated (data checked), rendered (viewport).
    readonly property var scene: root.forge.scene
    readonly property var inspection: scene.inspection
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: qsTr("Vista previa")
        color: Theme.text
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        visible: root.inspection.validated !== undefined
        text: root.inspection.validated === undefined ? "" : qsTr("Validado: %1 vértices · %2 triángulos · índices %3 · %4 recurso(s) externo(s)")
            .arg(root.inspection.validated.vertices).arg(root.inspection.validated.triangles)
            .arg(root.inspection.validated.indicesChecked ? qsTr("comprobados") : qsTr("sin índices"))
            .arg(root.inspection.resources.length)
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: root.inspection.declared !== undefined
        text: root.inspection.declared === undefined ? "" : qsTr("Declarado: %1 nodos · %2 mallas · %3 materiales · %4 texturas · %5 huesos · %6 animaciones · %7 morph targets")
            .arg(root.inspection.declared.nodes).arg(root.inspection.declared.meshes).arg(root.inspection.declared.materials)
            .arg(root.inspection.declared.textures).arg(root.inspection.declared.joints).arg(root.inspection.declared.animations)
            .arg(root.inspection.declared.morphTargets)
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: root.scene.hasModel ? qsTr("Renderizado en el visor (Qt Quick 3D RuntimeLoader).")
            : root.forge.blenderBusy ? (root.forge.statusMessage || qsTr("Convirtiendo modelo a GLB con Blender…"))
            : root.scene.message.length ? root.scene.message
            : qsTr("Las vistas previas admiten GLB y glTF 2.0. Al seleccionar un BLEND/FBX/OBJ en un proyecto guardado, se convierte automáticamente a GLB mediante el puente de Blender.")
        color: root.scene.state === "error" ? Theme.warn : Theme.textDim
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: (root.inspection.skins || []).length ? root.inspection.skins : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            text: qsTr("Armature %1 (%2 huesos): %3")
                .arg(modelData.name || qsTr("(sin nombre)"))
                .arg((modelData.joints || []).length)
                .arg((modelData.joints || []).join(", "))
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
    }
    // Drawable parts as RuntimeLoader created them (see ForgeSceneViewModel.partMatch).
    // Counts and the selected part come from ForgeSceneViewModel; the list reads ForgePartsModel (rows updated in place).
    readonly property int partCount: scene.partCount
    readonly property var selectedPart: scene.selectedPart >= 0 ? scene.selectedPartInfo : null
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: root.partCount > 0
        text: qsTr("Partes del modelo (%1)").arg(root.partCount)
        color: Theme.text
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        visible: root.partCount > 0
        text: root.scene.partMatch === "order"
            ? qsTr("Nombres del glTF asignados por orden de escena: materiales, skin, morph targets y jerarquía coinciden.")
            : root.scene.partMatch === "order-unconfirmed"
            ? qsTr("Asignación por orden no confirmada: alguna parte no coincide con su nodo glTF.")
            : qsTr("El visor creó %1 parte(s) para %2 nodo(s) con malla: sin nombres del glTF.")
                .arg(root.partCount).arg((root.inspection.meshNodes || []).length)
        color: root.scene.partMatch === "order" ? Theme.textMuted : Theme.warn
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: root.scene.partsModel
        delegate: RowLayout {
            id: partRow
            required property var modelData
            Layout.fillWidth: true
            spacing: 2
            CheckBox {
                checked: partRow.modelData.visible
                onToggled: root.scene.setPartVisible(partRow.modelData.index, checked)
                ToolTip.visible: hovered
                ToolTip.text: partRow.modelData.hiddenBy.length
                    ? qsTr("Visible, pero oculta porque %1 está oculta (Qt Quick 3D no dibuja los hijos de un nodo oculto).").arg(partRow.modelData.hiddenBy)
                    : qsTr("Mostrar u ocultar esta parte en el visor")
            }
            ItemDelegate {
                Layout.fillWidth: true
                highlighted: root.scene.selectedPart === partRow.modelData.index
                text: partRow.modelData.label
                    + (partRow.modelData.triangles !== undefined ? qsTr(" · %1 triángulos").arg(partRow.modelData.triangles) : "")
                    + (partRow.modelData.hiddenBy.length ? qsTr(" · oculta por %1").arg(partRow.modelData.hiddenBy) : "")
                onClicked: root.scene.selectPart(highlighted ? -1 : partRow.modelData.index)
            }
            ToolButton {
                visible: root.scene.partCount > 1
                text: root.isPartIsolated(partRow.modelData.index) ? qsTr("Todo") : qsTr("Solo")
                font.pixelSize: Theme.small
                onClicked: root.isolatePart(partRow.modelData.index)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Ver esta malla por separado (aislar)")
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: root.selectedPart !== null
        text: {
            const p = root.selectedPart;
            if (!p)
                return "";
            const size = qsTr("tamaño local %1 × %2 × %3").arg(p.size.x.toFixed(3)).arg(p.size.y.toFixed(3)).arg(p.size.z.toFixed(3));
            const rendered = qsTr("%1 material(es)%2%3").arg(p.materials)
                .arg(p.skinned ? qsTr(" · con skin") : "")
                .arg(p.morphTargets > 0 ? qsTr(" · %1 morph target(s)").arg(p.morphTargets) : "");
            if (p.node === undefined)
                return qsTr("%1: %2 · %3").arg(p.label).arg(rendered).arg(size);
            return qsTr("%1: nodo glTF %2 · malla «%3» · %4 primitiva(s) · %5 vértices · %6 triángulos · %7 · %8%9")
                .arg(p.label).arg(p.node).arg(p.meshName || qsTr("sin nombre")).arg(p.primitives).arg(p.vertices).arg(p.triangles)
                .arg(rendered).arg(size).arg(p.agrees ? "" : qsTr(" · no coincide con el nodo"));
        }
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        Layout.fillWidth: true
        visible: root.partCount > 0
        enabled: root.scene.anyPartHidden
        text: qsTr("Mostrar todas las partes")
        onClicked: root.scene.showAllParts()
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Recargar vista previa")
        enabled: root.forge.hasProject && root.scene.filePath.length > 0 && !root.scene.busy
        onClicked: root.scene.reload()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Vuelve a validar y cargar el archivo (por ejemplo, después de editarlo fuera del estudio).")
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Abrir asistente Convertir a Skyrim…")
        enabled: root.forge.hasProject && !!root.modelInfo.path
        onClicked: root.toolRequested("skyrim")
    }
}
