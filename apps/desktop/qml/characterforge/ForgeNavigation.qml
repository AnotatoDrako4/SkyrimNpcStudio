import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Tools of Character Forge, grouped by task (project, character, resources and 3D, verification and output).
// Every tool stays reachable; a group folds away with its header. Tools that need a project say so, but can
// still be opened: each panel shows its own disabled state and the reason. Arrow keys move the selection.
ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    property string currentTool: forge.workspace.tool || "project"
    signal toolSelected(string tool)
    spacing: Theme.sp(2)

    readonly property var groups: [
        { key: "project", title: qsTr("Proyecto"), tools: [
            { key: "project", glyph: "document", label: qsTr("Proyecto"), tip: qsTr("Nombre, descripción, guardado y estado del borrador .sncf"), needsProject: false }
        ] },
        { key: "character", title: qsTr("Personaje"), tools: [
            { key: "native", glyph: "person", label: qsTr("Crear NPC"), tip: qsTr("NPC nativo: identidad, estadísticas, arquetipos, voz, hechizos y perks"), needsProject: true },
            { key: "outfit", glyph: "tag", label: qsTr("Atuendos e inventario"), tip: qsTr("Atuendo, inventario y objetos del personaje"), needsProject: true }
        ] },
        { key: "resources", title: qsTr("Recursos y 3D"), tools: [
            { key: "import", glyph: "folder-open", label: qsTr("Importar modelo"), tip: qsTr("Importar un modelo GLB o glTF al proyecto"), needsProject: true },
            { key: "library", glyph: "library", label: qsTr("Biblioteca de modelos"), tip: qsTr("Modelos ya importados en tus proyectos"), needsProject: false },
            { key: "ai", glyph: "bulb", label: qsTr("Generar con IA"), tip: qsTr("Imágenes de referencia y generación asistida"), needsProject: true },
            { key: "blender", glyph: "link", label: qsTr("Blender y MCP"), tip: qsTr("Conexión con Blender para convertir modelos"), needsProject: true },
            { key: "skeleton", glyph: "people", label: qsTr("Esqueleto"), tip: qsTr("Huesos y compatibilidad con el esqueleto de Skyrim"), needsProject: true },
            { key: "materials", glyph: "light", label: qsTr("Materiales"), tip: qsTr("Texturas y materiales del modelo"), needsProject: true }
        ] },
        { key: "output", title: qsTr("Verificación y salida"), tools: [
            { key: "validation", glyph: "ok", label: qsTr("Validación"), tip: qsTr("Validación estructural y de comportamiento contra el perfil indexado"), needsProject: true },
            { key: "export", glyph: "export", label: qsTr("Exportar"), tip: qsTr("Exportación técnica; la exportación jugable sigue bloqueada sin FaceGen nativo"), needsProject: true }
        ] }
    ]
    readonly property var flatTools: {
        const all = []
        for (const g of groups) for (const t of g.tools) all.push(t)
        return all
    }
    property var folded: ({})

    function select(key) {
        root.toolSelected(key)
        if (root.forge.hasProject)
            root.forge.setWorkspaceValue("tool", key)
    }
    function step(delta) {
        const i = flatTools.findIndex(t => t.key === currentTool)
        const next = flatTools[Math.max(0, Math.min(flatTools.length - 1, i + delta))]
        if (next) select(next.key)
    }
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)

    // Project name and state
    Text {
        Layout.fillWidth: true
        text: root.forge.projectName || qsTr("Sin proyecto")
        color: root.forge.hasProject ? Theme.text : Theme.textDim
        font.family: Theme.fontFamily
        font.pixelSize: Theme.large
        font.bold: root.forge.hasProject
        elide: Text.ElideRight
        Layout.bottomMargin: Theme.sp(2)
    }
    Flow {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.sp(6)
        spacing: Theme.sp(4)
        StatusBadge {
            visible: root.forge.hasProject
            text: root.forge.dirty ? qsTr("SIN GUARDAR") : (root.forge.projectPath.length > 0 ? qsTr("GUARDADO") : qsTr("NUEVO"))
            tone: root.forge.dirty ? Theme.warn : Theme.ok
        }
        StatusBadge {
            visible: root.forge.hasProject
            text: qsTr("FACEGEN PENDIENTE")
            tone: Theme.textDim
        }
    }

    Repeater {
        model: root.groups
        delegate: ColumnLayout {
            id: group
            required property var modelData
            readonly property bool isFolded: root.folded[modelData.key] === true
            Layout.fillWidth: true
            spacing: Theme.sp(1)

            // Group header: folds the group.
            AbstractButton {
                id: groupHeader
                Layout.fillWidth: true
                Layout.topMargin: Theme.sp(6)
                implicitHeight: Theme.controlHeight - Theme.sp(6)
                Accessible.name: group.modelData.title
                Accessible.description: group.isFolded ? qsTr("Grupo plegado") : qsTr("Grupo desplegado")
                onClicked: {
                    const f = Object.assign({}, root.folded)
                    f[group.modelData.key] = !group.isFolded
                    root.folded = f
                }
                contentItem: RowLayout {
                    spacing: Theme.sp(4)
                    Icon {
                        name: group.isFolded ? "chevron-right" : "chevron-down"
                        size: Theme.small
                        color: Theme.textMuted
                    }
                    Text {
                        text: group.modelData.title.toUpperCase()
                        color: groupHeader.hovered ? Theme.text : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.small
                        font.bold: true
                        font.letterSpacing: 0.6
                        Layout.fillWidth: true
                    }
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: groupHeader.visualFocus ? Theme.focusWidth : 0
                    border.color: Theme.borderFocus
                    radius: Theme.radiusSmall
                }
            }

            Repeater {
                model: group.isFolded ? [] : group.modelData.tools
                delegate: ItemDelegate {
                    id: entry
                    required property var modelData
                    readonly property bool current: root.currentTool === modelData.key
                    readonly property bool waiting: modelData.needsProject && !root.forge.hasProject
                    objectName: "forgeTool:" + modelData.key
                    // The label stays in `text` (drawn by contentItem below): automation and accessibility read it.
                    text: modelData.label
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    leftPadding: Theme.sp(10)
                    rightPadding: Theme.sp(8)
                    highlighted: current
                    Accessible.name: modelData.label
                    Accessible.description: modelData.tip
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: modelData.tip + (waiting ? "\n" + qsTr("Requiere un proyecto: créalo o ábrelo primero.") : "")
                    onClicked: root.select(modelData.key)
                    contentItem: RowLayout {
                        spacing: Theme.sp(8)
                        Icon {
                            name: entry.modelData.glyph
                            color: entry.current ? Theme.accent : entry.waiting ? Theme.textMuted : Theme.textDim
                        }
                        Text {
                            text: entry.modelData.label
                            color: entry.current ? Theme.text : entry.waiting ? Theme.textMuted : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.normal
                            font.bold: entry.current
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Icon {
                            visible: entry.waiting
                            name: "lock"
                            size: Theme.small
                            color: Theme.textMuted
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: entry.current ? Theme.accentSubtle : entry.down ? Theme.accentDim : entry.hovered ? Theme.panelAlt : "transparent"
                        border.width: entry.visualFocus ? Theme.focusWidth : 0
                        border.color: Theme.borderFocus
                        Rectangle {
                            visible: entry.current
                            width: 3
                            height: parent.height - Theme.sp(10)
                            radius: 1.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.accent
                        }
                    }
                }
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.sp(8)
        text: qsTr("Borrador independiente .sncf: nada se escribe en Skyrim ni en MO2.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
    }
}
