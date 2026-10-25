import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Gallery of the head parts the selected NPC can use (worker catalog.searchHeadParts: race and sex already filtered),
// by category, with instant search and a plugin filter. A card only selects; «Probar en el NPC» applies it to the draft
// (AppearanceEditor.setHeadPart: undoable, rebuilt in the 3D view, refused when the face protection blocks head parts).
// There are no thumbnails yet: the 3D view in the centre is the preview, and the gallery says so instead of showing a picture.
ColumnLayout {
    id: root
    required property StudioController studio
    property bool allowed: true
    property string category: "Hair"
    property string selectedKey: ""
    property string modFilter: ""
    readonly property var options: root.studio.appearance.headPartOptions
    readonly property var current: {
        const keys = {}
        for (const p of root.studio.appearance.headParts) keys[(p.formKey || "").toLowerCase()] = true
        return keys
    }
    readonly property var visibleOptions: root.modFilter === "" ? root.options
                                          : root.options.filter(o => o.plugin === root.modFilter)
    readonly property var selected: {
        for (const o of root.options) if (o.formKey === root.selectedKey) return o
        return null
    }
    readonly property var plugins: {
        const seen = {}
        const out = []
        for (const o of root.options) if (!seen[o.plugin]) { seen[o.plugin] = true; out.push(o.plugin) }
        return out.sort()
    }
    spacing: Theme.spacingSm
    Layout.fillWidth: true

    // One search per (NPC, category, text), whatever order the NPC, the face analysis and the protection settle in: a query that
    // could not run (head parts blocked or still analysing) runs as soon as it is allowed. A new NPC empties the options
    // (AppearanceEditor.set_target), and its FormKey makes a new query.
    property string lastQuery: ""
    readonly property string npcKey: (root.studio.npc.summary || {}).formKey || ""
    readonly property string query: root.npcKey + "|" + root.category + "|" + searchField.text.trim()
    function search() {
        if (!root.allowed || root.npcKey === "" || root.query === root.lastQuery) return
        root.lastQuery = root.query
        root.selectedKey = ""
        root.studio.appearance.searchHeadParts(root.category, searchField.text)
    }
    readonly property var searchState: root.studio.appearance.headPartSearch
    property int retries: 0
    onQueryChanged: { root.retries = 0; searchDelay.restart() }
    onAllowedChanged: searchDelay.restart()
    // A failed search (worker busy, timeout) is retried a few times instead of leaving an empty gallery for good.
    onSearchStateChanged: {
        if (root.searchState.state === "error" && root.retries < 3) { root.retries += 1; root.lastQuery = ""; retryDelay.restart() }
        // "none": the editor was (re)targeted and dropped the options, even for the same NPC and query: search again.
        else if (root.searchState.state === "none") { root.lastQuery = ""; searchDelay.restart() }
    }
    Timer { id: retryDelay; interval: 1500; onTriggered: root.search() }
    Connections {
        target: root.studio
        // Loading an NPC (even the same one again) empties the options: its next query must run.
        function onNpcChanged() { if (root.studio.npcLoading) root.lastQuery = ""; else searchDelay.restart() }
    }
    Timer { id: searchDelay; interval: 250; onTriggered: root.search() }

    Text {
        text: qsTr("Galería de cabello, ojos y piezas de cabeza")
        color: Theme.textDim
        font.pixelSize: Theme.small
        Accessible.role: Accessible.Heading
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        enabled: root.allowed
        Repeater {
            model: [
                { type: "Hair", label: qsTr("Cabello") },
                { type: "Eyes", label: qsTr("Ojos") },
                { type: "Eyebrows", label: qsTr("Cejas") },
                { type: "FacialHair", label: qsTr("Barba") },
                { type: "Scars", label: qsTr("Cicatrices") }
            ]
            delegate: Button {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: root.category === modelData.type
                Accessible.name: qsTr("Categoría %1").arg(modelData.label)
                onClicked: { root.category = modelData.type; root.modFilter = "" }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        enabled: root.allowed
        TextField {
            id: searchField
            objectName: "headPartSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Buscar por nombre, EditorID o plugin…")
            Accessible.name: qsTr("Buscar piezas de cabeza")
        }
        ComboBox {
            id: modBox
            Layout.preferredWidth: Math.max(140, root.width * 0.35)
            Accessible.name: qsTr("Filtrar por plugin")
            model: [qsTr("Todos los plugins")].concat(root.plugins)
            currentIndex: root.modFilter === "" ? 0 : Math.max(0, root.plugins.indexOf(root.modFilter) + 1)
            onActivated: index => root.modFilter = index === 0 ? "" : root.plugins[index - 1]
        }
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.micro
        color: root.searchState.state === "error" ? Theme.error : Theme.textDim
        text: !root.allowed ? ""
              : root.searchState.state === "searching" ? qsTr("Buscando piezas compatibles…")
              : root.searchState.state === "error" ? qsTr("La búsqueda falló (%1).%2").arg(root.searchState.error || "?")
                                                        .arg(root.retries < 3 ? qsTr(" Reintentando…") : "")
              : root.searchState.state === "ready" && root.options.length === 0 ? qsTr("No hay piezas de esta categoría compatibles con la raza y el sexo del NPC.")
              : qsTr("%1 resultado(s) compatibles con la raza y el sexo del NPC. Sin miniaturas: el visor 3D muestra el resultado al probar.")
                    .arg(root.visibleOptions.length)
    }

    GridView {
        id: grid
        objectName: "headPartGrid"
        Layout.fillWidth: true
        readonly property int columns: Math.max(1, Math.floor(width / 190))
        cellWidth: Math.floor(width / columns)
        cellHeight: Theme.rowHeight * 2 + 8
        Layout.preferredHeight: Math.min(Math.ceil(count / columns) * cellHeight, cellHeight * 4)
        // A blocked head (face protection) shows no options at all: only the reason, below the gallery.
        visible: count > 0 && root.allowed
        clip: true
        enabled: root.allowed
        model: root.visibleOptions
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        Accessible.name: qsTr("Piezas de cabeza disponibles")
        ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar piezas de cabeza") }
        delegate: Item {
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                id: card
                anchors.fill: parent
                anchors.margins: 3
                radius: Theme.radius
                readonly property bool inUse: !!root.current[(modelData.formKey || "").toLowerCase()]
                readonly property bool chosen: modelData.formKey === root.selectedKey
                color: chosen ? Theme.selection : cardArea.containsMouse ? Theme.panelAlt : Theme.panel
                border.color: chosen ? Theme.accent : inUse ? Theme.ok : Theme.border
                border.width: chosen || inUse ? 2 : 1
                Accessible.role: Accessible.Button
                Accessible.name: (modelData.name || modelData.editorId) + (inUse ? qsTr(", en uso") : "")
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name || modelData.editorId || modelData.formKey
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!modelData.name && modelData.name !== modelData.editorId
                        text: modelData.editorId || ""
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.plugin + (root.studio.providerOf(modelData.plugin) ? " · " + root.studio.providerOf(modelData.plugin) : "")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideMiddle
                    }
                    StatusBadge {
                        visible: card.inUse
                        text: qsTr("En uso")
                        tone: Theme.ok
                    }
                }
                MouseArea {
                    id: cardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.selectedKey = modelData.formKey
                    onDoubleClicked: { root.selectedKey = modelData.formKey; root.studio.appearance.setHeadPart(modelData.formKey, modelData.type) }
                }
                ToolTip.visible: cardArea.containsMouse
                ToolTip.delay: 600
                ToolTip.text: (modelData.modelPath || qsTr("sin malla")) + "\n" + modelData.formKey
            }
        }
    }

    // Actions stay in place below the grid, whatever the number of results (they wrap in a narrow panel).
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: root.allowed
        Button {
            objectName: "headPartTry"
            text: qsTr("Probar en el NPC")
            enabled: root.selected !== null && !root.current[(root.selectedKey || "").toLowerCase()]
            highlighted: true
            Accessible.name: root.selected ? qsTr("Probar %1 en el NPC").arg(root.selected.name || root.selected.editorId) : text
            onClicked: root.studio.appearance.setHeadPart(root.selected.formKey, root.selected.type)
        }
        Button {
            text: qsTr("Deshacer")
            enabled: root.studio.history.canUndo
            Accessible.name: qsTr("Deshacer: %1").arg(root.studio.history.undoText)
            ToolTip.visible: hovered
            ToolTip.text: root.studio.history.undoText
            onClicked: root.studio.history.undo()
        }
        Button {
            text: qsTr("Pieza de la raza")
            Accessible.name: qsTr("Quitar la pieza elegida y volver a la de la raza")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Quita la pieza de esta categoría del NPC: vuelve la que la raza da por defecto")
            onClicked: root.studio.appearance.removeHeadPart(root.category)
        }
    }
    Text {
        Layout.fillWidth: true
        visible: root.selected !== null
        wrapMode: Text.WrapAnywhere
        color: Theme.textDim
        font.pixelSize: Theme.micro
        text: root.selected ? qsTr("Seleccionada: %1 (%2) · %3").arg(root.selected.name || root.selected.editorId)
                              .arg(root.selected.formKey).arg(root.selected.modelPath || qsTr("sin malla")) : ""
    }
}
