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
    spacing: 0

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
    // Used by the empty viewer ("Buscar NPC") and the command palette.
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }
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

    // Search header: always on screen. The filters fold away; how many are active and a way to clear them stay.
    property bool filtersOpen: false
    readonly property int activeFilters: (sexBox.currentIndex > 0 ? 1 : 0) + (kindBox.currentIndex > 0 ? 1 : 0)
                                         + (pluginField.text.length > 0 ? 1 : 0) + (appearanceBox.currentIndex > 0 ? 1 : 0)
    function clearFilters() {
        sexBox.currentIndex = 0
        kindBox.currentIndex = 0
        pluginField.text = ""
        if (appearanceBox.currentIndex !== 0) {
            appearanceBox.currentIndex = 0
            root.studio.setAppearanceFilter("")
        }
        root.search()
    }
    // Ctrl+C on a list row or a card: the FormKey goes to the clipboard (the stable identity of the NPC), through
    // the same StudioController.copyToClipboard the catalog context menus use.
    function copyFormKey(formKey) {
        if (!formKey) return
        root.studio.copyToClipboard(formKey)
        copiedNote.text = qsTr("FormKey copiado: %1").arg(formKey)
        copiedTimer.restart()
    }
    Timer { id: copiedTimer; interval: 2500; onTriggered: copiedNote.text = "" }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        TextField {
            id: searchField
            objectName: "npcSearchField"
            Layout.fillWidth: true
            leftPadding: Theme.sp(30)
            rightPadding: clearSearch.visible ? clearSearch.width + Theme.sp(4) : Theme.sp(8)
            placeholderText: qsTr("Buscar NPC: nombre, EditorID, FormKey o FormID")
            Accessible.name: qsTr("Buscar NPC")
            ToolTip.visible: hovered && text.length === 0
            ToolTip.delay: 800
            ToolTip.text: qsTr("Nombre, EditorID, FormKey (0A2C8E:Skyrim.esm) o FormID (000A2C8E). Flecha abajo: ir a los resultados.")
            enabled: root.studio.phase === StudioController.Ready
            onTextChanged: debounce.restart()
            Keys.onDownPressed: (root.viewMode === "gallery" ? grid : list).forceActiveFocus()
            Icon {
                name: "search"
                size: Theme.small + 1
                color: Theme.textMuted
                anchors.left: parent.left
                anchors.leftMargin: Theme.sp(10)
                anchors.verticalCenter: parent.verticalCenter
            }
            IconToolButton {
                id: clearSearch
                visible: searchField.text.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: searchField.height - 4
                glyph: "close"
                label: qsTr("Borrar la búsqueda")
                showLabel: false
                onClicked: searchField.clear()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            SegmentedControl {
                objectName: "npcViewSwitch"
                model: [{ label: qsTr("Lista"), glyph: "list" }, { label: qsTr("Galería"), glyph: "grid" }]
                currentIndex: root.viewMode === "gallery" ? 1 : 0
                compact: root.width < Theme.px(260)
                onActivated: index => root.setViewMode(index === 1 ? "gallery" : "list")
            }
            Item { Layout.fillWidth: true }
            IconToolButton {
                objectName: "npcFiltersButton"
                glyph: "filter"
                label: root.activeFilters > 0 ? qsTr("Filtros (%1)").arg(root.activeFilters) : qsTr("Filtros")
                showLabel: root.width >= Theme.px(240)
                checkable: true
                checked: root.filtersOpen
                tone: root.activeFilters > 0 ? Theme.accent : Theme.text
                tip: root.filtersOpen ? qsTr("Ocultar los filtros") : qsTr("Sexo, tipo, plugin y apariencia")
                onClicked: root.filtersOpen = !root.filtersOpen
            }
            IconToolButton {
                objectName: "npcClearFiltersButton"
                visible: root.activeFilters > 0
                glyph: "clear"
                label: qsTr("Limpiar filtros")
                showLabel: false
                onClicked: root.clearFilters()
            }
        }
        GridLayout {
            objectName: "npcFilters"
            visible: root.filtersOpen
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingSm
            rowSpacing: Theme.spacingSm
            ComboBox {
                id: sexBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                Accessible.name: qsTr("Filtro de sexo")
                model: [{ text: qsTr("Sexo: todos"), value: "" }, { text: qsTr("Mujeres"), value: "female" }, { text: qsTr("Hombres"), value: "male" }]
                onActivated: root.search()
            }
            ComboBox {
                id: kindBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                Accessible.name: qsTr("Filtro de tipo de NPC")
                model: [{ text: qsTr("Tipo: todos"), value: "" }, { text: qsTr("Únicos"), value: "unique" },
                        { text: qsTr("Genéricos"), value: "generic" }, { text: qsTr("Con plantilla"), value: "templated" },
                        { text: qsTr("Presets de creación"), value: "preset" }]
                onActivated: root.search()
            }
            TextField {
                id: pluginField
                Layout.fillWidth: true
                Layout.columnSpan: 2
                placeholderText: qsTr("Plugin que define o modifica (p. ej. Helen_Lydia_replacer.esp)")
                Accessible.name: qsTr("Filtro de plugin")
                onTextChanged: debounce.restart()
            }
            ComboBox {
                id: appearanceBox
                objectName: "npcAppearanceFilter"
                Layout.fillWidth: true
                Layout.columnSpan: 2
                textRole: "text"
                valueRole: "value"
                Accessible.name: qsTr("Filtro de apariencia")
                model: [{ text: qsTr("Apariencia: todos"), value: "" }, { text: qsTr("Con avisos faciales"), value: "warning" },
                        { text: qsTr("Casos ambiguos (revisión)"), value: "review" }, { text: qsTr("Sin analizar"), value: "unanalysed" }]
                onActivated: root.studio.setAppearanceFilter(currentValue)
            }
        }
        Text {
            objectName: "npcResultCount"
            Layout.fillWidth: true
            text: {
                const shown = root.viewMode === "gallery" ? grid.count : list.count
                if (root.studio.phase !== StudioController.Ready)
                    return qsTr("El catálogo aparece cuando termina de indexarse el perfil.")
                if (root.studio.appearanceFilter !== "")
                    return qsTr("Mostrando %1 de %2 cargados con este filtro (%3 resultados en total)").arg(shown).arg(root.studio.npcLoaded).arg(root.studio.npcTotal)
                if (root.studio.npcTotal === 0)
                    return root.activeFilters > 0 || searchField.text.length > 0
                           ? qsTr("Ningún NPC coincide con la búsqueda o los filtros.") : qsTr("Sin resultados.")
                return qsTr("Mostrando %1 de %2").arg(shown).arg(root.studio.npcTotal)
                       + (root.studio.npcLoadingMore ? qsTr(" · cargando más…") : "")
            }
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
        Text {
            id: copiedNote
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.ok
            font.pixelSize: Theme.small
            elide: Text.ElideMiddle
        }
        Text {
            Layout.fillWidth: true
            visible: root.studio.appearanceFilter !== "" && root.studio.appearanceScan.stage !== "done"
            text: qsTr("El filtro de apariencia usa el análisis del perfil: ejecútalo en la pestaña Origen.")
            color: Theme.warn
            font.pixelSize: Theme.small
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
        ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar la lista de NPC") }
        onAtYEndChanged: root.loadMoreIfAtEnd()
        onContentHeightChanged: Qt.callLater(root.loadMoreIfAtEnd)
        // Keyboard: arrows move, Space selects, Enter opens the editor, Ctrl+C copies the FormKey.
        currentIndex: -1
        keyNavigationEnabled: true
        activeFocusOnTab: true
        Keys.onSpacePressed: if (currentItem) root.studio.selectNpc(currentItem.formKey)
        Keys.onReturnPressed: if (currentItem) { root.studio.selectNpc(currentItem.formKey); root.editRequested(currentItem.formKey) }
        Keys.onEnterPressed: if (currentItem) { root.studio.selectNpc(currentItem.formKey); root.editRequested(currentItem.formKey) }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier) && currentItem) {
                root.copyFormKey(currentItem.formKey)
                event.accepted = true
            }
        }
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
            leftPadding: Theme.sp(10)
            rightPadding: Theme.sp(10)
            highlighted: root.isSelected(formKey)
            Accessible.name: (name.trim().length > 0 ? name : editorId) + ", " + formKey
            ToolTip.visible: hovered
            ToolTip.delay: 900
            ToolTip.text: !hovered ? "" : (name.trim().length > 0 ? name : qsTr("(sin nombre)")) + "\n" + editorId + " · " + formKey
                          + "\n" + qsTr("Gana: %1").arg(winningPlugin) + (winningMod ? " [" + winningMod + "]" : "")
                          + "\n" + qsTr("Doble clic o Enter: editar · Ctrl+C: copiar FormKey")
            onClicked: { ListView.view.currentIndex = index; ListView.view.forceActiveFocus(); root.studio.selectNpc(formKey) }
            onDoubleClicked: root.editRequested(formKey)
            background: Rectangle {
                color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                border.width: item.ListView.isCurrentItem && item.ListView.view.activeFocus ? Theme.focusWidth : 0
                border.color: Theme.borderFocus
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
                    color: Theme.textMuted
                    font.pixelSize: Theme.small
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
        // Portrait (square) + name + three short lines, at the real text sizes (scale and legibility floor).
        cellHeight: cellWidth - 10 + Theme.normal + 3 * (Theme.small + 3) + 18
        cacheBuffer: cellHeight  // one row beyond the visible ones
        currentIndex: -1
        keyNavigationEnabled: true
        ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar la galería de NPC") }
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
        Keys.onPressed: event => {
            if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier) && currentItem) {
                root.copyFormKey(currentItem.formKey)
                event.accepted = true
            }
        }

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
                        Layout.topMargin: 2
                        text: card.name.trim().length > 0 ? card.name : card.editorId
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (card.race || "?") + " · " + (card.sex === "female" ? "♀" : "♂")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.winningPlugin
                        color: Theme.textMuted
                        font.pixelSize: Theme.small
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.stateText(card.appearanceState)
                        color: root.stateColor(card.appearanceState)
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                }
            }
            ToolTip.visible: cardArea.containsMouse
            ToolTip.delay: 900
            ToolTip.text: !cardArea.containsMouse ? "" : (card.name.trim().length > 0 ? card.name : qsTr("(sin nombre)")) + "\n" + card.editorId + " · " + card.formKey
                          + "\n" + qsTr("Gana: %1").arg(card.winningPlugin) + (card.winningMod ? " [" + card.winningMod + "]" : "")
                          + "\n" + root.stateText(card.appearanceState)
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
                font.pixelSize: Theme.small
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: inspectorColumn.facegen ? qsTr("FaceGen: «%1», %2").arg(inspectorColumn.facegen.mod || "?").arg(root.kindText(inspectorColumn.facegen.kind))
                                              : qsTr("FaceGen: sin datos")
                color: inspectorColumn.facegen && inspectorColumn.facegen.kind === "missing" ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: inspectorColumn.facetint ? qsTr("FaceTint: «%1», %2").arg(inspectorColumn.facetint.mod || "?").arg(root.kindText(inspectorColumn.facetint.kind))
                                               : qsTr("FaceTint: sin datos")
                color: inspectorColumn.facetint && inspectorColumn.facetint.kind === "missing" ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                visible: inspectorColumn.warnings.length > 0
                text: qsTr("%1 aviso(s): %2").arg(inspectorColumn.warnings.length).arg(inspectorColumn.warnings.length > 0 ? inspectorColumn.warnings[0].message : "")
                color: Theme.warn
                font.pixelSize: Theme.small
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
                font.pixelSize: Theme.small
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
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "npcPortraitMissing"
                Layout.fillWidth: true
                readonly property var missing: root.studio.portraits.stateOf(inspectorColumn.control.formKey || "", root.studio.portraits.revision).missing || []
                visible: missing.length > 0
                text: qsTr("El retrato se dibujó sin %1 archivo(s) que el perfil no tiene: %2").arg(missing.length).arg(missing.join(", "))
                color: Theme.warn
                font.pixelSize: Theme.small
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
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.preferredWidth: 72
                            text: (versionBox.version.createdAt || "").slice(0, 16).replace("T", " ")
                            color: Theme.textMuted
                            font.pixelSize: Theme.small
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
                font.pixelSize: Theme.small
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
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideMiddle
            }
            Text {
                id: captureMessage
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.warn
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Button {
                    objectName: "npcPortraitRefresh"
                    text: qsTr("Revalidar")
                    font.pixelSize: Theme.small
                    enabled: inspectorColumn.control.formKey !== undefined
                    onClicked: root.studio.portraits.refresh(inspectorColumn.control.formKey)
                }
                Button {
                    objectName: "npcAttachSkyrimCapture"
                    text: qsTr("Adjuntar captura de Skyrim…")
                    font.pixelSize: Theme.small
                    enabled: inspectorColumn.control.formKey !== undefined
                    onClicked: captureDialog.open()
                }
                Button {
                    text: qsTr("Quitar captura")
                    font.pixelSize: Theme.small
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
