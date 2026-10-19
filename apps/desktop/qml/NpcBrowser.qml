import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// NPC explorer: instant search by name, EditorID, FormKey or xEdit FormID, with filters. The same results
// show as a list or as a gallery of cards (Space selects, Enter or a double click opens the editor).
ColumnLayout {
    id: root
    required property StudioController studio
    property string viewMode: AppSettings.npcBrowserView  // "list" or "gallery" (Configuración › NPC y retratos; remembered)
    function setViewMode(mode) {
        root.viewMode = mode
        AppSettings.npcBrowserView = mode
    }
    // Double click or Enter on an NPC: the host opens the editor.
    signal editRequested(string formKey)
    spacing: 6

    function search() {
        studio.searchNpcs(searchField.text, sexBox.currentValue, kindBox.currentValue, pluginField.text)
    }
    // The next page of the same search when a view reaches its end (or a filter leaves it short).
    function loadMoreIfAtEnd() {
        const view = root.viewMode === "gallery" ? grid : list
        if (!view.visible || root.studio.npcLoaded === 0 || root.studio.npcLoaded >= root.studio.npcTotal) return
        // No row shown: the appearance filter hid the whole page. contentHeight 0 with rows: not laid out yet
        // (a view just shown reports atYEnd before its first layout), so it waits for its layout.
        if (view.count === 0 || (view.contentHeight > 0 && view.atYEnd)) root.studio.loadMoreNpcs()
    }
    function isSelected(formKey) {
        return root.studio.npc.summary !== undefined && root.studio.npc.summary.formKey === formKey
    }
    function reviewText(state) {
        return state === "approved" ? qsTr("aprobada") : state === "rejected" ? qsTr("rechazada")
             : state === "deferred" ? qsTr("aplazada") : state === "stale" ? qsTr("caducada") : qsTr("pendiente")
    }
    // What the last profile scan found for this NPC (Origen); nothing is inferred without a scan.
    function stateText(state) {
        if (!state || state.worst === undefined) return qsTr("Sin analizar")
        if (state.requiresReview) return qsTr("Caso ambiguo · revisión %1").arg(root.reviewText(state.reviewState))
        return state.worst === "warning" ? qsTr("Avisos faciales") : state.worst === "info" ? qsTr("Notas") : qsTr("Registro y FaceGen encajan")
    }
    function stateColor(state) {
        if (!state || state.worst === undefined) return Theme.textMuted
        if (state.requiresReview) return state.reviewState === "approved" ? Theme.ok : Theme.warn
        return state.worst === "warning" ? Theme.warn : state.worst === "info" ? Theme.textDim : Theme.ok
    }
    function component(key) {
        return (root.studio.appearanceControl.components || []).find(c => c.key === key)
    }
    function kindText(kind) {
        return kind === "loose" ? qsTr("archivo suelto") : kind === "bsa" ? qsTr("dentro de un BSA")
             : kind === "missing" ? qsTr("no existe en el perfil") : (kind || "?")
    }

    Timer { id: debounce; interval: AppSettings.searchDebounceMs; onTriggered: root.search() }
    Connections {
        target: root.studio
        function onLoaded() { root.search() }
        function onNpcResultsChanged() { Qt.callLater(root.loadMoreIfAtEnd) }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        spacing: 4
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Nombre, EditorID, FormKey (0A2C8E:Skyrim.esm) o FormID (000A2C8E)")
            enabled: root.studio.phase === StudioController.Ready
            onTextChanged: debounce.restart()
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: sexBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{ text: qsTr("Sexo: todos"), value: "" }, { text: qsTr("Mujeres"), value: "female" }, { text: qsTr("Hombres"), value: "male" }]
                onActivated: root.search()
            }
            ComboBox {
                id: kindBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{ text: qsTr("Tipo: todos"), value: "" }, { text: qsTr("Únicos"), value: "unique" },
                        { text: qsTr("Genéricos"), value: "generic" }, { text: qsTr("Con plantilla"), value: "templated" },
                        { text: qsTr("Presets de creación"), value: "preset" }]
                onActivated: root.search()
            }
        }
        TextField {
            id: pluginField
            Layout.fillWidth: true
            placeholderText: qsTr("Plugin que define o modifica (p. ej. Helen_Lydia_replacer.esp)")
            onTextChanged: debounce.restart()
        }
        ComboBox {
            id: appearanceBox
            objectName: "npcAppearanceFilter"
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [{ text: qsTr("Apariencia: todos"), value: "" }, { text: qsTr("Con avisos faciales"), value: "warning" },
                    { text: qsTr("Casos ambiguos (revisión)"), value: "review" }, { text: qsTr("Sin analizar"), value: "unanalysed" }]
            onActivated: root.studio.setAppearanceFilter(currentValue)
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "npcListModeButton"
                Layout.fillWidth: true
                text: qsTr("Lista")
                checkable: true
                checked: root.viewMode === "list"
                onClicked: root.setViewMode("list")
            }
            Button {
                objectName: "npcGalleryModeButton"
                Layout.fillWidth: true
                text: qsTr("Galería")
                checkable: true
                checked: root.viewMode === "gallery"
                onClicked: root.setViewMode("gallery")
            }
        }
        Text {
            Layout.fillWidth: true
            text: {
                const shown = root.viewMode === "gallery" ? grid.count : list.count
                if (root.studio.appearanceFilter !== "")
                    return qsTr("%1 de %2 cargados con este filtro (%3 resultados en total)").arg(shown).arg(root.studio.npcLoaded).arg(root.studio.npcTotal)
                return (root.studio.npcTotal > shown ? qsTr("%1 resultados (mostrando %2)").arg(root.studio.npcTotal).arg(shown)
                                                     : qsTr("%1 resultados").arg(root.studio.npcTotal))
                       + (root.studio.npcLoadingMore ? qsTr(" · cargando más…") : "")
            }
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: root.studio.appearanceFilter !== "" && root.studio.appearanceScan.stage !== "done"
            text: qsTr("El filtro de apariencia usa el análisis del perfil: ejecútalo en la pestaña Origen.")
            color: Theme.warn
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }
    }

    ListView {
        id: list
        visible: root.viewMode === "list"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: visible ? root.studio.npcResults : null
        ScrollBar.vertical: ScrollBar {}
        onAtYEndChanged: root.loadMoreIfAtEnd()
        onContentHeightChanged: Qt.callLater(root.loadMoreIfAtEnd)
        delegate: ItemDelegate {
            id: item
            required property int index
            required property string formKey
            required property string formId
            required property string name
            required property string editorId
            required property string race
            required property string sex
            required property string winningPlugin
            required property string winningMod
            required property int overrideCount
            width: ListView.view.width
            highlighted: root.isSelected(formKey)
            onClicked: root.studio.selectNpc(formKey)
            onDoubleClicked: root.editRequested(formKey)
            background: Rectangle {
                color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; opacity: 0.5 }
            }
            contentItem: ColumnLayout {
                spacing: 1
                RowLayout {
                    Text {
                        text: (item.name.trim().length > 0 ? item.name : qsTr("(sin nombre)")) + "  "
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    StatusBadge {
                        visible: item.overrideCount > 1
                        text: item.overrideCount + "×"
                        tone: item.overrideCount > 2 ? Theme.warn : Theme.accent
                    }
                }
                Text {
                    text: item.editorId + " · " + item.formId + " · " + (item.race || "?") + " · " + (item.sex === "female" ? "♀" : "♂")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("gana: ") + item.winningPlugin + (item.winningMod ? "  [" + item.winningMod + "]" : "")
                    color: Theme.textDim
                    font.pixelSize: Theme.small - 1
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }

    // ---- gallery: the same results as cards
    GridView {
        id: grid
        objectName: "npcGallery"
        visible: root.viewMode === "gallery"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        focus: visible
        model: visible ? root.studio.npcResults : null
        readonly property int columns: Math.max(1, Math.floor(width / AppSettings.galleryCardWidth))
        cellWidth: Math.floor(width / columns)
        cellHeight: cellWidth - 10 + 66
        cacheBuffer: cellHeight  // one row beyond the visible ones
        currentIndex: -1
        keyNavigationEnabled: true
        ScrollBar.vertical: ScrollBar {}
        onAtYEndChanged: root.loadMoreIfAtEnd()
        onContentHeightChanged: { Qt.callLater(root.loadMoreIfAtEnd); prefetchTimer.restart() }
        onContentYChanged: prefetchTimer.restart()
        onCountChanged: prefetchTimer.restart()

        // While nothing visible is waiting, the portrait engine prepares the rows the user will scroll to next
        // (Configuración › Rendimiento). Only real rows of the current results; a new list replaces the old one.
        Timer {
            id: prefetchTimer
            interval: 350
            onTriggered: {
                const rows = AppSettings.portraitPrefetchRows
                if (!grid.visible || rows <= 0 || grid.count === 0) { root.studio.portraits.prefetch([]); return }
                const firstHidden = grid.indexAt(1, grid.contentY + grid.height + grid.cellHeight * 1.5)
                const start = firstHidden >= 0 ? firstHidden : Math.min(grid.count, Math.ceil((grid.contentY + grid.height) / grid.cellHeight + 1) * grid.columns)
                const keys = []
                for (let i = start; i < Math.min(grid.count, start + rows * grid.columns); ++i) {
                    const row = root.studio.npcResults.get(i)
                    if (row && row.formKey) keys.push(row.formKey)
                }
                root.studio.portraits.prefetch(keys)
            }
        }

        function selectCurrent() {
            if (currentItem) root.studio.selectNpc(currentItem.formKey)
        }
        function openCurrent() {
            if (!currentItem) return
            root.studio.selectNpc(currentItem.formKey)
            root.editRequested(currentItem.formKey)
        }
        Keys.onSpacePressed: selectCurrent()
        Keys.onReturnPressed: openCurrent()
        Keys.onEnterPressed: openCurrent()

        delegate: Item {
            id: card
            required property int index
            required property string formKey
            required property string name
            required property string editorId
            required property string race
            required property string sex
            required property string winningPlugin
            required property string winningMod
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property var appearanceState: { root.studio.appearanceScan; return root.studio.appearanceStateOf(card.formKey) }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: 4
                color: root.isSelected(card.formKey) ? Theme.accentDim : cardArea.containsMouse ? Theme.panelAlt : Theme.panel
                border.color: card.GridView.isCurrentItem && grid.activeFocus ? Theme.borderFocus : Theme.border
                border.width: card.GridView.isCurrentItem && grid.activeFocus ? 2 : 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 1
                    // A render of the studio from the profile's own resources (or the stored one, marked "verificando",
                    // while it is checked), or why there is none: never a stand-in face.
                    PortraitThumb {
                        id: portraitBox
                        Layout.fillWidth: true
                        Layout.preferredHeight: width
                        portraits: root.studio.portraits
                        formKey: card.formKey
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.name.trim().length > 0 ? card.name : card.editorId
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (card.race || "?") + " · " + (card.sex === "female" ? "♀" : "♂")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.winningPlugin
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.stateText(card.appearanceState)
                        color: root.stateColor(card.appearanceState)
                        font.pixelSize: Theme.micro
                        elide: Text.ElideRight
                    }
                }
            }
            MouseArea {
                id: cardArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    grid.currentIndex = card.index
                    grid.forceActiveFocus()
                    root.studio.selectNpc(card.formKey)
                }
                onDoubleClicked: root.editRequested(card.formKey)
            }
        }
    }

    // ---- inspector of the selected NPC (Appearance Control Map), under the gallery
    Rectangle {
        objectName: "npcGalleryInspector"
        visible: root.viewMode === "gallery" && root.studio.appearanceControl.formKey !== undefined
        Layout.fillWidth: true
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        Layout.bottomMargin: 6
        implicitHeight: inspectorColumn.implicitHeight + 10
        color: Theme.panelAlt
        radius: 4
        border.color: Theme.border
        ColumnLayout {
            id: inspectorColumn
            anchors.fill: parent
            anchors.margins: 5
            spacing: 1
            readonly property var control: root.studio.appearanceControl
            readonly property var facegen: { control; return root.component("facegen") }
            readonly property var facetint: { control; return root.component("facetint") }
            readonly property var warnings: (control.findings || []).filter(f => f.severity === "warning")
            Text {
                Layout.fillWidth: true
                text: qsTr("Datos faciales: %1 (mod «%2»)").arg(inspectorColumn.control.facePlugin || "?").arg(inspectorColumn.control.faceMod || "?")
                color: Theme.text
                font.pixelSize: Theme.micro
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: inspectorColumn.facegen ? qsTr("FaceGen: «%1», %2").arg(inspectorColumn.facegen.mod || "?").arg(root.kindText(inspectorColumn.facegen.kind))
                                              : qsTr("FaceGen: sin datos")
                color: inspectorColumn.facegen && inspectorColumn.facegen.kind === "missing" ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.micro
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: inspectorColumn.facetint ? qsTr("FaceTint: «%1», %2").arg(inspectorColumn.facetint.mod || "?").arg(root.kindText(inspectorColumn.facetint.kind))
                                               : qsTr("FaceTint: sin datos")
                color: inspectorColumn.facetint && inspectorColumn.facetint.kind === "missing" ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.micro
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                visible: inspectorColumn.warnings.length > 0
                text: qsTr("%1 aviso(s): %2").arg(inspectorColumn.warnings.length).arg(inspectorColumn.warnings.length > 0 ? inspectorColumn.warnings[0].message : "")
                color: Theme.warn
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: inspectorColumn.control.resolution !== undefined && inspectorColumn.control.resolution.requiresReview === true
                text: qsTr("Caso ambiguo: revisión %1. Se decide en la pestaña Origen; nunca se aprueba solo.")
                          .arg(root.reviewText(inspectorColumn.control.resolution ? inspectorColumn.control.resolution.reviewState : ""))
                color: Theme.warn
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                readonly property var portrait: root.studio.portraits.stateOf(inspectorColumn.control.formKey || "", root.studio.portraits.revision)
                text: portrait.state === "ready"
                      ? qsTr("Retrato: render del estudio con los recursos del perfil (%1); no es una captura de Skyrim.")
                            .arg(portrait.source === "cache" ? qsTr("de la caché") : qsTr("generado ahora"))
                      : portrait.state === "error" ? qsTr("Retrato: no disponible (%1)").arg(portrait.reason)
                      : qsTr("Retrato: sin generar")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "npcPortraitMissing"
                Layout.fillWidth: true
                readonly property var missing: root.studio.portraits.stateOf(inspectorColumn.control.formKey || "", root.studio.portraits.revision).missing || []
                visible: missing.length > 0
                text: qsTr("El retrato se dibujó sin %1 archivo(s) que el perfil no tiene: %2").arg(missing.length).arg(missing.join(", "))
                color: Theme.warn
                font.pixelSize: Theme.micro
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 4
                elide: Text.ElideRight
            }

            // Versions kept by the portrait cache, each read back with its hash: the current render, the render
            // it replaced (compared only when the same renderer made both) and a Skyrim capture the user attached.
            readonly property var versions: root.studio.portraits.versionsOf(control.formKey || "", root.studio.portraits.revision)
            RowLayout {
                objectName: "npcPortraitVersions"
                Layout.fillWidth: true
                visible: inspectorColumn.versions.current !== undefined || inspectorColumn.versions.skyrimCapture !== undefined
                spacing: 4
                Repeater {
                    model: [
                        { key: "current", label: qsTr("Render actual") },
                        { key: "previous", label: qsTr("Render anterior") },
                        { key: "skyrimCapture", label: qsTr("Captura de Skyrim") }
                    ].filter(v => inspectorColumn.versions[v.key] !== undefined)
                    delegate: ColumnLayout {
                        id: versionBox
                        required property var modelData
                        readonly property var version: inspectorColumn.versions[modelData.key]
                        objectName: "npcPortraitVersion_" + modelData.key
                        Layout.preferredWidth: 72
                        spacing: 1
                        Rectangle {
                            Layout.preferredWidth: 72
                            Layout.preferredHeight: 72
                            color: Theme.surface
                            radius: 3
                            border.color: versionBox.modelData.key === "skyrimCapture" ? Theme.warn : Theme.border
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: versionBox.version.url
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                sourceSize: Qt.size(144, 144)
                            }
                        }
                        Text {
                            Layout.preferredWidth: 72
                            text: versionBox.modelData.label
                            color: versionBox.modelData.key === "skyrimCapture" ? Theme.warn : Theme.textDim
                            font.pixelSize: Theme.micro
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.preferredWidth: 72
                            text: (versionBox.version.createdAt || "").slice(0, 16).replace("T", " ")
                            color: Theme.textMuted
                            font.pixelSize: Theme.micro
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                objectName: "npcPortraitComparison"
                Layout.fillWidth: true
                visible: inspectorColumn.versions.previous !== undefined
                readonly property var changed: inspectorColumn.versions.changedResources || []
                text: inspectorColumn.versions.comparable !== true
                      ? qsTr("Render anterior de otra versión del renderizador (%1): encuadre o luces distintos, no se compara.")
                            .arg(inspectorColumn.versions.previous ? inspectorColumn.versions.previous.rendererVersion : "?")
                      : changed.length > 0
                        ? qsTr("Entre ambos renders cambiaron %1 recurso(s): %2").arg(changed.length).arg(changed.join(", "))
                        : inspectorColumn.versions.profileChanged === true
                          ? qsTr("Mismos recursos faciales; cambió el perfil o el orden de carga.")
                          : qsTr("Mismos recursos registrados en ambos renders.")
                color: Theme.textDim
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: inspectorColumn.versions.skyrimCapture !== undefined
                text: qsTr("La captura de Skyrim la aportaste tú (%1); el estudio no la verifica ni la usa como retrato.")
                          .arg(inspectorColumn.versions.skyrimCapture ? inspectorColumn.versions.skyrimCapture.file : "")
                color: Theme.warn
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideMiddle
            }
            Text {
                id: captureMessage
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.warn
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Button {
                    objectName: "npcPortraitRefresh"
                    text: qsTr("Revalidar")
                    font.pixelSize: Theme.micro
                    enabled: inspectorColumn.control.formKey !== undefined
                    onClicked: root.studio.portraits.refresh(inspectorColumn.control.formKey)
                }
                Button {
                    objectName: "npcAttachSkyrimCapture"
                    text: qsTr("Adjuntar captura de Skyrim…")
                    font.pixelSize: Theme.micro
                    enabled: inspectorColumn.control.formKey !== undefined
                    onClicked: captureDialog.open()
                }
                Button {
                    text: qsTr("Quitar captura")
                    font.pixelSize: Theme.micro
                    visible: inspectorColumn.versions.skyrimCapture !== undefined
                    onClicked: root.studio.portraits.removeGameCapture(inspectorColumn.control.formKey)
                }
            }
            FileDialog {
                id: captureDialog
                title: qsTr("Captura tomada dentro de Skyrim")
                nameFilters: [qsTr("Imágenes (*.png *.jpg *.jpeg *.bmp)")]
                onAccepted: captureMessage.text = root.studio.portraits.attachGameCapture(inspectorColumn.control.formKey, selectedFile)
            }
        }
    }
}
