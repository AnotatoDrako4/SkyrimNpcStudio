import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// BodySlide presets for the selected NPC through Body Change NG (studio.body = BodyDirector).
// Operation A (assign a preset of the NPC's own body family) is kept apart from operation B (changing the body family),
// which is only evaluated here. Nothing is written until "Exportar distribución", which builds a NEW mod folder.
ColumnLayout {
    id: root
    objectName: "bodyPresetSection"
    required property StudioController studio
    readonly property var director: root.studio.body
    readonly property var env: root.director.environment
    readonly property var npc: root.director.npc
    readonly property var draft: root.director.draft
    readonly property var result: root.director.exportResult
    property string selectedUid: ""
    readonly property var selected: {
        const list = root.director.presets
        for (let i = 0; i < list.length; ++i) if (list[i].uid === root.selectedUid) return list[i]
        return null
    }
    property string assignMessage: ""
    spacing: Theme.spacingMd
    Layout.fillWidth: true

    function verdictTone(v) {
        if (v === "compatible") return Theme.ok
        if (v === "experimental") return Theme.warn
        if (v === "needs-resources") return Theme.warn
        if (v === "blocked") return Theme.error
        return Theme.textDim
    }
    function applyFilter() {
        root.director.setFilter(searchField.text, familyBox.currentValue, assignableOnly.checked)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Carpeta donde crear el mod (fuera de MO2 y de Skyrim)")
        onAccepted: outputField.text = selectedFolder.toString().replace(/^file:\/{3}/, "")
    }

    Text {
        text: qsTr("Presets de BodySlide (Body Change NG)")
        color: Theme.accent
        font.bold: true
        font.pixelSize: Theme.fs(13)
        Accessible.role: Accessible.Heading
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: qsTr("Asigna al NPC un preset de su misma familia corporal. Cambiar de familia (3BA ↔ UBE) es otra operación y aquí solo se evalúa.")
    }

    // ---------------------------------------------------------------- profile
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: envCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: Theme.border

        ColumnLayout {
            id: envCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Text {
                visible: root.env.state !== "ready"
                text: root.env.state === "loading" ? qsTr("Leyendo presets y Body Change NG del perfil…")
                      : root.env.state === "error" ? qsTr("Error: %1").arg(root.env.error)
                      : qsTr("Carga un perfil de MO2.")
                color: root.env.state === "error" ? Theme.error : Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Flow {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                spacing: 6
                StatusBadge {
                    text: root.env.bcng ? qsTr("Body Change NG ✓") : qsTr("Sin Body Change NG")
                    tone: root.env.bcng ? Theme.ok : Theme.error
                    ToolTip.visible: bcngHover.hovered
                    ToolTip.text: root.env.bcng ? qsTr("SKSE\\Plugins\\BodyChangeNG.dll de «%1»").arg(root.env.bcng) : qsTr("SKSE\\Plugins\\BodyChangeNG.dll no está en el perfil")
                    HoverHandler { id: bcngHover }
                }
                StatusBadge {
                    visible: !!root.env.obody
                    text: qsTr("OBody NG activo (choque)")
                    tone: Theme.error
                }
                StatusBadge {
                    text: root.env.racemenu ? qsTr("RaceMenu BodyMorph") : qsTr("Sin RaceMenu")
                    tone: root.env.racemenu ? Theme.ok : Theme.warn
                }
                StatusBadge {
                    text: qsTr("Familia global: %1").arg(root.env.globalFamily ?? "")
                    tone: Theme.textDim
                }
            }
            Text {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: Theme.small
                text: qsTr("%1 presets en %2 archivos (%3 nombres repetidos, %4 archivos ilegibles). Frameworks activos: %5.")
                      .arg(root.env.presets).arg(root.env.files).arg(root.env.duplicates).arg(root.env.unreadableFiles)
                      .arg(root.env.installedFamilies)
            }
            Text {
                visible: root.env.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.env.distributionError ? Theme.error : Theme.textDim
                font.pixelSize: Theme.small
                text: root.env.distributionError ? qsTr("Distribución actual ilegible (%1): %2").arg(root.env.distributionProvider).arg(root.env.distributionError)
                      : root.env.distributionProvider ? qsTr("Distribución actual: %1 regla(s) de «%2».").arg(root.env.distributionRules).arg(root.env.distributionProvider)
                      : qsTr("El perfil no tiene BodyChangeNGdistribution.json.")
            }
        }
    }

    // ---------------------------------------------------------------- NPC body
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: npcCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: Theme.border

        ColumnLayout {
            id: npcCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: root.npc.state === "ready" ? qsTr("Cuerpo de %1").arg(root.npc.name || root.npc.editorId)
                          : root.npc.state === "inspecting" ? qsTr("Analizando el cuerpo del NPC…")
                          : root.npc.state === "error" ? qsTr("No se pudo analizar el cuerpo")
                          : qsTr("Elige un NPC para ver su cuerpo")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                StatusBadge {
                    visible: root.npc.state === "ready"
                    text: root.npc.familyLabel ?? ""
                    tone: root.npc.family === "unknown" ? Theme.warn : Theme.accent
                }
            }
            Text {
                visible: root.npc.state === "error"
                text: root.npc.error ?? ""
                color: Theme.error
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                visible: root.npc.state === "ready"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: root.npc.state !== "ready" ? "" : qsTr("Malla: %1 (%2)").arg(root.npc.body.path || qsTr("ninguna"))
                      .arg(root.npc.body.provider || qsTr("no está en el perfil"))
                      + "\n" + (root.npc.body.tri ? qsTr("TRI de morphs: %1 (%2)").arg(root.npc.body.tri).arg(root.npc.body.triProvider || qsTr("falta"))
                                                  : qsTr("Sin TRI de morphs (BODYTRI)"))
            }
            Repeater {
                model: root.npc.state === "ready" ? root.npc.evidence : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "• " + modelData
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            IssueList {
                visible: root.npc.state === "ready"
                issues: root.npc.issues ?? []
                emptyText: qsTr("El cuerpo se identificó sin incidencias.")
            }
        }
    }

    // ---------------------------------------------------------------- operation A: presets
    Text {
        text: qsTr("Asignar un preset")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: searchField
            objectName: "bodyPresetSearch"
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            placeholderText: qsTr("Buscar preset, archivo o mod…")
            Accessible.name: qsTr("Buscar presets de BodySlide")
            onTextChanged: searchTimer.restart()
            Timer { id: searchTimer; interval: 200; onTriggered: root.applyFilter() }
        }
        ComboBox {
            id: familyBox
            Layout.preferredWidth: Math.min(implicitWidth, root.width * 0.45)
            Layout.minimumWidth: 90
            textRole: "text"
            valueRole: "value"
            Accessible.name: qsTr("Filtrar por familia corporal")
            model: [
                { text: qsTr("Todas las familias"), value: "" },
                { text: "CBBE 3BA", value: "3ba" },
                { text: "CBBE", value: "cbbe" },
                { text: "UBE", value: "ube" },
                { text: qsTr("Otras"), value: "other" },
                { text: qsTr("Desconocida"), value: "unknown" }
            ]
            onActivated: root.applyFilter()
        }
    }
    CheckBox {
        id: assignableOnly
        text: qsTr("Solo asignables")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Oculta los presets bloqueados y los que requieren recursos que el perfil no tiene")
        onToggled: root.applyFilter()
    }
    Text {
        text: qsTr("%1 presets").arg(root.director.presetTotal) + (root.director.presetTotal > root.director.presets.length
              ? qsTr(" (se muestran %1; afina la búsqueda)").arg(root.director.presets.length) : "")
        color: Theme.textDim
        font.pixelSize: Theme.small
    }

    ListView {
        id: presetList
        objectName: "bodyPresetList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, Theme.rowHeight * 8)
        clip: true
        model: root.director.presets
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Accessible.name: qsTr("Presets de BodySlide del perfil")
        ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar presets") }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: presetList.width
            highlighted: modelData.uid === root.selectedUid
            Accessible.name: modelData.name + ", " + modelData.familyLabel + ", " + modelData.verdictLabel
            onClicked: { root.selectedUid = modelData.uid; root.assignMessage = ""; presetList.currentIndex = index }
            contentItem: RowLayout {
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: (modelData.assigned ? "✔ " : "") + modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: modelData.assigned
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.provider + " · " + modelData.source
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideMiddle
                    }
                }
                StatusBadge { text: modelData.familyLabel; tone: Theme.textDim }
                StatusBadge {
                    visible: modelData.assigned
                    text: qsTr("En 3D ✓")
                    tone: Theme.ok
                }
                StatusBadge {
                    visible: modelData.verdict !== ""
                    text: modelData.verdictLabel
                    tone: root.verdictTone(modelData.verdict)
                }
            }
        }
    }

    Rectangle {
        visible: root.selected !== null
        Layout.fillWidth: true
        implicitHeight: selCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: root.selected ? root.verdictTone(root.selected.verdict) : Theme.border

        ColumnLayout {
            id: selCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: root.selected ? root.selected.name : ""
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: root.selected ? qsTr("Archivo: %1\nMod: %2\nFamilia: %3 — %4\n%5 sliders%6")
                      .arg(root.selected.file).arg(root.selected.provider).arg(root.selected.familyLabel).arg(root.selected.reason)
                      .arg(root.selected.sliders).arg(root.selected.duplicate ? qsTr(" · nombre repetido en otro archivo") : "") : ""
            }
            IssueList {
                issues: root.selected ? root.selected.issues : []
                emptyText: qsTr("Sin incidencias para este NPC.")
            }
            CheckBox {
                id: acceptExperimental
                visible: root.selected !== null && root.selected.verdict === "experimental"
                text: qsTr("Acepto que es experimental")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("El resultado solo se sabrá dentro del juego; lo probaré en un perfil de prueba")
            }
            RowLayout {
                spacing: 8
                Button {
                    objectName: "bodyAssignButton"
                    text: (root.selected && root.selected.assigned) ? qsTr("Reaplicar en 3D") : qsTr("Asignar y visualizar en 3D")
                    enabled: root.selected !== null && root.npc.state === "ready"
                             && (root.selected.verdict === "compatible" || (root.selected.verdict === "experimental" && acceptExperimental.checked))
                    Accessible.name: root.npc.state === "ready" ? qsTr("Asignar el preset a %1").arg(root.npc.name || root.npc.editorId) : text
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Aplica los morphs de este preset en la malla corporal del visor 3D y crea la regla BCNG para el NPC")
                    onClicked: root.assignMessage = root.director.assign(root.selectedUid, acceptExperimental.checked)
                }
                Button {
                    visible: root.selected !== null && root.selected.assigned
                    text: qsTr("Desasignar")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Quita la regla y restaura el cuerpo base en el visor 3D")
                    onClicked: {
                        root.director.unassign()
                        root.assignMessage = ""
                    }
                }
                StatusBadge {
                    visible: root.selected !== null && root.selected.assigned
                    text: qsTr("Activo en 3D ✓")
                    tone: Theme.ok
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.assignMessage !== ""
                text: root.assignMessage
                color: Theme.error
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---------------------------------------------------------------- operation B: family change (evaluation only)
    Text {
        text: qsTr("Cambiar de familia corporal (solo evaluación)")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: root.npc.state === "ready" ? root.npc.changes : []
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: changeCol.implicitHeight + 12
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.border
            ColumnLayout {
                id: changeCol
                anchors.fill: parent
                anchors.margins: 6
                spacing: 3
                RowLayout {
                    Text {
                        text: qsTr("A %1").arg(modelData.label)
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    StatusBadge { text: modelData.verdictLabel; tone: root.verdictTone(modelData.verdict) }
                }
                IssueList { issues: modelData.issues }
            }
        }
    }

    // ---------------------------------------------------------------- draft and export
    Text {
        text: qsTr("Distribución a exportar")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.normal
        Accessible.role: Accessible.Heading
    }
    Text {
        visible: (root.draft.rules ?? []).length === 0
        text: qsTr("Todavía no hay reglas. Asigna un preset a un NPC para crear una.")
        color: Theme.textDim
        font.pixelSize: Theme.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Repeater {
        model: root.draft.rules ?? []
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 → %2%3").arg(modelData.npcName).arg(modelData.preset).arg(modelData.file ? " (" + modelData.file + ")" : "")
                color: Theme.text
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            StatusBadge { text: modelData.verdictLabel; tone: root.verdictTone(modelData.verdict) }
            Button {
                text: qsTr("Quitar")
                Accessible.name: qsTr("Quitar la regla de %1").arg(modelData.npcName)
                onClicked: root.director.removeRule(modelData.id)
            }
        }
    }
    Text {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: Theme.small
        text: root.draft.baseProvider ? qsTr("Se combinará con las %1 reglas de «%2» (las del estudio van primero): %3 reglas en total.")
                                         .arg(root.draft.baseRules).arg(root.draft.baseProvider).arg(root.draft.mergedRules)
                                       : qsTr("%1 reglas en total.").arg(root.draft.mergedRules)
    }
    Repeater {
        model: root.draft.errors ?? []
        delegate: Text {
            required property var modelData
            Layout.fillWidth: true
            text: "✖ " + modelData
            color: Theme.error
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
    }
    GridLayout {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 6
        rowSpacing: 4
        Text { text: qsTr("Nombre del mod"); color: Theme.textDim; font.pixelSize: Theme.small }
        TextField {
            id: modNameField
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: "SNS Body Distribution"
            Accessible.name: qsTr("Nombre del mod a crear")
        }
        Text { text: qsTr("Carpeta"); color: Theme.textDim; font.pixelSize: Theme.small }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: outputField
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                text: root.director.defaultExportDirectory
                Accessible.name: qsTr("Carpeta donde se crea el mod")
            }
            Button {
                text: qsTr("Elegir…")
                Accessible.name: qsTr("Elegir carpeta de exportación")
                onClicked: folderDialog.open()
            }
        }
    }
    CheckBox {
        id: zipBox
        visible: (root.draft.rules ?? []).length > 0
        checked: true
        text: qsTr("Crear también un ZIP")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Un ZIP que se instala con «Instalar mod desde archivo» de MO2")
    }
    RowLayout {
        visible: (root.draft.rules ?? []).length > 0
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        Button {
            text: qsTr("Vaciar")
            Accessible.name: qsTr("Vaciar el borrador de la distribución")
            onClicked: root.director.clearDraft()
        }
        Button {
            objectName: "bodyExportButton"
            text: root.result.state === "exporting" ? qsTr("Exportando…") : qsTr("Exportar")
            enabled: (root.draft.errors ?? []).length === 0 && root.result.state !== "exporting"
            highlighted: true
            Accessible.name: qsTr("Exportar la distribución de Body Change NG como un mod nuevo")
            onClicked: root.director.exportDistribution(modNameField.text, outputField.text, zipBox.checked)
        }
    }
    Rectangle {
        visible: root.result.state === "done" || root.result.state === "error"
        Layout.fillWidth: true
        implicitHeight: resCol.implicitHeight + 16
        color: Theme.panelAlt
        radius: Theme.radius
        border.color: root.result.state === "done" ? Theme.ok : Theme.error
        ColumnLayout {
            id: resCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 3
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: root.result.state === "done" ? Theme.ok : Theme.error
                font.pixelSize: Theme.small
                font.bold: true
                text: root.result.state === "done" ? qsTr("Mod creado y verificado por lectura: %1").arg(root.result.modDirectory)
                                                   : qsTr("No se exportó: %1").arg(root.result.error ?? "")
            }
            Text {
                visible: root.result.state === "done" && !!root.result.zipPath
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.textDim
                font.pixelSize: Theme.small
                text: qsTr("ZIP: %1").arg(root.result.zipPath ?? "")
            }
            Repeater {
                model: root.result.state === "done" ? root.result.files : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    font.family: Theme.mono
                    text: modelData.path + " · " + modelData.bytes + " B · SHA-256 " + modelData.sha256
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warn
                font.pixelSize: Theme.small
                text: qsTr("Estado en el juego: PENDIENTE_SKYRIM. Instálalo en un perfil de prueba y compruébalo dentro de Skyrim.")
            }
        }
    }
}
