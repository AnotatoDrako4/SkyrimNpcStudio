import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Inspector of the portrait cache of the loaded profile (studio.portraits.cacheReport): every entry of the folder with whether it
// is still in use and why not, the images no entry names, and the removal of only what cannot be used again. A render that is
// valid, the render kept for comparison and the Skyrim captures the user attached are never offered for removal. Removing is
// asked of the host (removeUnusableRequested / clearVariantsRequested), which confirms it. Nothing here is a Skyrim capture.
Popup {
    id: dialog
    objectName: "portraitCacheInspector"
    required property StudioController studio
    readonly property PortraitRenderer portraits: studio.portraits
    readonly property var report: portraits.cacheReport
    readonly property string state: report.state ?? "idle"
    readonly property var counts: report.counts ?? ({})
    property string filter: "attention"
    signal removeUnusableRequested()
    signal clearVariantsRequested()

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: parent ? Math.min(parent.width - 2 * Theme.spacingXl, Theme.px(900)) : Theme.px(800)
    height: parent ? Math.min(parent.height - 2 * Theme.spacingXl, Theme.px(660)) : Theme.px(560)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    background: Rectangle { color: Theme.bg; radius: Theme.radiusLarge; border.color: Theme.border }
    onOpened: portraits.inspectCache()

    readonly property var filters: [
        { id: "attention", label: qsTr("Requieren decisión"), test: r => r.unusable },
        { id: "valid", label: qsTr("Vigentes"), test: r => r.kind === "render" && r.state === "valid" },
        { id: "kept", label: qsTr("Anteriores y capturas"), test: r => r.kind === "previous" || r.kind === "capture" },
        { id: "all", label: qsTr("Todo"), test: r => true }
    ]
    // Portraits "Rehacer los desactualizados" would ask for (stale or damaged renders of a known NPC).
    readonly property int redoable: (report.rows ?? []).filter(r => r.kind === "render" && (r.state === "stale" || r.state === "damaged") && r.formKey).length
    readonly property var shown: {
        const f = filters.find(x => x.id === filter) ?? filters[0]
        return (report.rows ?? []).filter(f.test)
    }

    // Names next to the FormKeys: asked of the catalog for the worst rows first (200 per request), shown when they arrive.
    function npcName(formKey) { return formKey ? (studio.npcNames[studio.npcKey(formKey)] ?? "") : "" }
    function askNames() {
        const keys = (report.rows ?? []).slice(0, 400).map(r => r.formKey).filter(k => k)
        studio.resolveNpcNames(keys)
        studio.resolveNpcNames(keys)
    }
    Connections {
        target: dialog.portraits
        function onCacheReportChanged() { dialog.askNames() }
    }
    function kindText(k) {
        return k === "render" ? qsTr("Retrato") : k === "previous" ? qsTr("Anterior") : k === "capture" ? qsTr("Captura de Skyrim") : qsTr("Suelto")
    }
    function stateText(r) {
        return r.state === "valid" ? qsTr("vigente") : r.state === "stale" ? qsTr("desactualizado") : r.state === "damaged" ? qsTr("dañado")
             : r.state === "kept" ? qsTr("se conserva") : r.code === "orphan-image" ? qsTr("huérfano") : qsTr("ajeno")
    }
    function toneOf(r) {
        return r.state === "valid" ? Theme.ok : r.state === "stale" || r.state === "orphan" ? Theme.warn : r.state === "damaged" ? Theme.error : Theme.textDim
    }
    function reasonText(r) {
        switch (r.code) {
        case "renderer-changed": return qsTr("Cambió el renderizador (%1).").arg(r.detail)
        case "profile-changed": return qsTr("Cambió el perfil de MO2 o el orden de carga.")
        case "resource-changed": return r.changed > 1 ? qsTr("Cambió un archivo: %1 y %2 más.").arg(r.detail).arg(r.changed - 1) : qsTr("Cambió un archivo: %1.").arg(r.detail)
        case "image-missing": return qsTr("Falta la imagen %1.").arg(r.detail)
        case "image-hash": return qsTr("La imagen %1 no coincide con la huella guardada.").arg(r.detail)
        case "entry-unreadable": return qsTr("No se puede leer la entrada: %1.").arg(r.detail)
        case "entry-invalid": return qsTr("La entrada no es válida: %1.").arg(r.detail)
        case "other-npc": return qsTr("La entrada pertenece a otro NPC (%1).").arg(r.detail)
        case "unknown-source": return qsTr("Origen desconocido (%1).").arg(r.detail)
        case "orphan-image": return qsTr("Imagen que ninguna entrada nombra: %1.").arg(r.detail)
        case "unknown-file": return qsTr("Archivo que no es del estudio (no se toca): %1.").arg(r.detail)
        default:
            return r.kind === "previous" ? qsTr("Render anterior: se guarda para compararlo y se espera que difiera del perfil.")
                 : r.kind === "capture" ? qsTr("Captura que adjuntaste: el estudio nunca la borra por su cuenta.")
                 : qsTr("Coincide con el perfil, el renderizador y la huella de su imagen.")
        }
    }
    function bytesText(b) {
        return b >= 1048576 ? qsTr("%1 MB").arg((b / 1048576).toFixed(1)) : qsTr("%1 KB").arg(Math.max(1, Math.round(b / 1024)))
    }

    contentItem: ColumnLayout {
        spacing: 0
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Inspector de la caché de retratos")

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: titleRow.implicitHeight + 2 * Theme.spacingMd
            color: Theme.header
            radius: Theme.radiusLarge
            RowLayout {
                id: titleRow
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Inspector de la caché de retratos")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.large
                }
                Button { objectName: "inspectorRefresh"; text: qsTr("Volver a comprobar"); enabled: dialog.state !== "running"; onClicked: dialog.portraits.inspectCache() }
                Button { objectName: "inspectorClose"; text: qsTr("Cerrar"); onClicked: dialog.close() }
            }
        }

        Text {
            objectName: "inspectorSummary"
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSm
            color: dialog.state === "unavailable" ? Theme.warn : Theme.text
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
            text: dialog.state === "unavailable" ? (dialog.report.reason ?? "")
                : dialog.state !== "ready" && dialog.state !== "running" ? qsTr("Sin comprobar.")
                : dialog.state === "running" ? qsTr("Comprobando cada imagen contra el perfil…")
                : qsTr("%1 vigentes · %2 desactualizados · %3 dañados · %4 sueltos · %5 anteriores · %6 capturas de Skyrim · %7 en total · comprobado en %8 ms")
                      .arg(dialog.counts.valid ?? 0).arg(dialog.counts.stale ?? 0).arg(dialog.counts.damaged ?? 0).arg(dialog.counts.orphans ?? 0)
                      .arg(dialog.counts.kept ?? 0).arg(dialog.counts.captures ?? 0).arg(dialog.bytesText(dialog.report.bytes ?? 0)).arg(dialog.report.ms ?? 0)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            spacing: Theme.spacingXs
            visible: dialog.state === "ready" || dialog.state === "running"
            Repeater {
                model: dialog.filters
                delegate: Button {
                    required property var modelData
                    objectName: "inspectorFilter_" + modelData.id
                    text: modelData.label
                    checkable: true
                    checked: dialog.filter === modelData.id
                    onClicked: dialog.filter = modelData.id
                }
            }
            Item { Layout.fillWidth: true }
        }

        ListView {
            id: list
            objectName: "inspectorList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingSm
            clip: true
            model: dialog.state === "ready" || dialog.state === "running" ? dialog.shown : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "inspectorRow_" + index
                width: ListView.view.width
                implicitHeight: rowLayout.implicitHeight + 10
                height: implicitHeight
                color: index % 2 === 0 ? "transparent" : Theme.panelAlt
                border.color: Theme.border
                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: Theme.spacingSm
                    StatusBadge { text: dialog.stateText(row.modelData); tone: dialog.toneOf(row.modelData); Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            objectName: "inspectorRowTitle_" + row.index
                            text: dialog.kindText(row.modelData.kind) + (row.modelData.formKey ? " · " + (dialog.npcName(row.modelData.formKey) ? dialog.npcName(row.modelData.formKey) + " · " : "") + row.modelData.formKey : "")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: dialog.reasonText(row.modelData)
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Text {
                        text: dialog.bytesText(row.modelData.bytes) + (row.modelData.createdAt ? "\n" + row.modelData.createdAt.replace("T", " ").substring(0, 16) : "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.micro
                        horizontalAlignment: Text.AlignRight
                        Layout.alignment: Qt.AlignTop
                    }
                    Button {
                        objectName: "inspectorRedo_" + row.index
                        visible: row.modelData.kind === "render" && row.modelData.formKey.length > 0 && row.modelData.state !== "valid"
                        text: qsTr("Rehacer")
                        Accessible.name: qsTr("Rehacer el retrato de %1").arg(row.modelData.formKey)
                        onClicked: dialog.portraits.refresh(row.modelData.formKey)
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: (dialog.state === "ready") && list.count === 0
                text: dialog.filter === "attention" ? qsTr("Nada que decidir: todo lo que hay en la caché se puede usar.") : qsTr("No hay entradas de este tipo.")
                color: Theme.textDim
                font.pixelSize: Theme.small
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            visible: dialog.state === "ready" && dialog.report.truncated === true
            text: qsTr("Se muestran las primeras %1 de %2 filas (las peores primero).").arg((dialog.report.rows ?? []).length).arg(dialog.report.total ?? 0)
            color: Theme.textMuted
            font.pixelSize: Theme.micro
        }

        Text {
            objectName: "inspectorLastPurge"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            readonly property var purge: dialog.report.lastPurge ?? null
            visible: purge !== null && dialog.state === "ready"
            text: purge ? (purge.error ? qsTr("No se pudo quitar todo: %1").arg(purge.error)
                                      : qsTr("Último vaciado selectivo: %1 entradas, %2 archivos, %3.").arg(purge.rows).arg(purge.files).arg(dialog.bytesText(purge.bytes))) : ""
            color: purge && purge.error ? Theme.warn : Theme.textDim
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }

        // Redo the out-of-date portraits, a few at a time, with progress. Nothing is deleted: each render keeps the previous one.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            spacing: Theme.spacingSm
            readonly property var redo: dialog.portraits.redoProgress
            readonly property bool running: redo.state === "running" || redo.state === "cancelling"
            Button {
                objectName: "inspectorRedoStale"
                text: qsTr("Rehacer los desactualizados (%1)").arg(dialog.redoable)
                enabled: dialog.state === "ready" && dialog.redoable > 0 && !parent.running
                onClicked: dialog.portraits.redoStale()
            }
            ProgressBar {
                objectName: "inspectorRedoBar"
                Layout.fillWidth: true
                visible: parent.running || parent.redo.state === "done" || parent.redo.state === "cancelled"
                from: 0
                to: Math.max(1, parent.redo.total ?? 1)
                value: parent.redo.done ?? 0
            }
            Text {
                objectName: "inspectorRedoText"
                visible: parent.redo.state !== "idle"
                color: (parent.redo.errors ?? 0) > 0 || parent.redo.state === "unavailable" ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.micro
                text: parent.redo.state === "unavailable" ? (parent.redo.reason ?? "")
                    : parent.redo.state === "done" ? qsTr("Rehechos %1 de %2 en %3 s%4").arg(parent.redo.done).arg(parent.redo.total).arg(Math.round(parent.redo.elapsedMs / 1000)).arg(parent.redo.errors > 0 ? qsTr(" · %1 con error: %2").arg(parent.redo.errors).arg(parent.redo.lastError) : "")
                    : parent.redo.state === "cancelled" ? qsTr("Cancelado: %1 de %2 rehechos").arg(parent.redo.done).arg(parent.redo.total)
                    : qsTr("Rehaciendo %1 de %2…").arg(parent.redo.done).arg(parent.redo.total)
            }
            Button {
                objectName: "inspectorRedoCancel"
                visible: parent.running
                text: parent.redo.state === "cancelling" ? qsTr("Cancelando…") : qsTr("Cancelar")
                enabled: parent.redo.state === "running"
                onClicked: dialog.portraits.cancelRedo()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSm
            spacing: Theme.spacingSm
            Button {
                objectName: "inspectorRemoveUnusable"
                text: qsTr("Quitar lo inservible (%1 entradas, ≈ %2)").arg(dialog.report.unusableRows ?? 0).arg(dialog.bytesText(dialog.report.unusableBytes ?? 0))
                enabled: dialog.state === "ready" && (dialog.report.unusableRows ?? 0) > 0
                onClicked: dialog.removeUnusableRequested()
            }
            Button {
                objectName: "inspectorClearVariants"
                visible: ((dialog.report.variants ?? {}).files ?? 0) > 0
                text: qsTr("Vaciar imágenes del A/B (%1 archivos, %2)").arg((dialog.report.variants ?? {}).files ?? 0).arg(dialog.bytesText((dialog.report.variants ?? {}).bytes ?? 0))
                enabled: dialog.state === "ready"
                onClicked: dialog.clearVariantsRequested()
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Solo se quitan retratos desactualizados o dañados e imágenes sueltas del estudio. Los vigentes, los anteriores y tus capturas de Skyrim no se tocan.")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
        }
    }
}
