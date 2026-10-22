import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Configuración (Ctrl+,): every setting whose effect exists, by category, with a search over titles,
// descriptions and keywords. Values come from AppSettings (theme, viewer, portraits...) and, without duplicating
// them, from WorkbenchState (scale, text, density, font, layouts). Closing it never touches a project or the scene.
Dialog {
    id: center
    required property StudioController studio
    property string category: "general"
    property string highlightId: ""
    objectName: "settingsCenter"
    title: qsTr("Configuración")
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Theme.px(1080), (parent ? parent.width : 1100) - Theme.px(40))
    height: Math.min(Theme.px(760), (parent ? parent.height : 760) - Theme.px(40))
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // Opens on a category, or on the setting a search result points to.
    function openAt(categoryKey, settingId) {
        center.category = categoryKey || center.category
        center.highlightId = settingId || ""
        if (!center.opened) center.open()
        searchField.text = ""
        Qt.callLater(center.revealHighlight)
    }
    function revealHighlight() {
        if (highlightId === "") return
        for (let i = 0; i < pageRepeater.count; ++i) {
            const item = pageRepeater.itemAt(i)
            if (item && item.setting.id === highlightId) {
                pageFlick.contentY = Math.max(0, Math.min(item.y - Theme.spacingLg, pageFlick.contentHeight - pageFlick.height))
                item.forceActiveFocus()
                return
            }
        }
    }
    function changedIn(key) {
        AppSettings.revision
        return AppSettings.settingsOf(key).filter(s => s.backend !== "panel" && !AppSettings.isDefault(s.id)).length
    }
    function categoryTitle(key) {
        const c = AppSettings.categories.find(x => x.key === key)
        return c ? c.title : key
    }
    function formatBytes(bytes) {
        if (bytes === undefined) return "?"
        return bytes > 1024 * 1024 ? (bytes / 1024 / 1024).toFixed(1) + " MB" : Math.round(bytes / 1024) + " KB"
    }
    // Automation (--settings-check): the search text and the page shown.
    function setSearch(text) { searchField.text = text }
    function searchCount() { return resultsView.count }
    function searchFocused() { return searchField.activeFocus }

    onOpened: {
        searchField.forceActiveFocus()  // SET-01: the search has the focus
        Qt.callLater(center.revealHighlight)
    }
    onCategoryChanged: pageFlick.contentY = 0

    header: Rectangle {
        implicitHeight: Theme.toolbarHeight + Theme.spacingSm
        color: Theme.header
        border.color: Theme.borderSubtle
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingXl
            anchors.rightMargin: Theme.spacingLg
            spacing: Theme.spacingLg
            Text {
                text: "⚙ " + qsTr("Configuración")
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.title
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: searchField
                objectName: "settingsSearchField"
                Layout.preferredWidth: Theme.px(360)
                placeholderText: qsTr("Buscar ajustes…  (tema, escala, retrato, cámara, MO2)")
                Accessible.name: qsTr("Buscar ajustes")
                Keys.onDownPressed: if (resultsView.count > 0) { resultsView.currentIndex = 0; resultsView.forceActiveFocus() }
                Keys.onReturnPressed: if (resultsView.count > 0) center.openAt(resultsView.model[0].category, resultsView.model[0].id)
            }
            Text { text: "Ctrl+,"; color: Theme.textMuted; font.pixelSize: Theme.small }
            ToolButton {
                text: "✕"
                implicitHeight: Theme.controlHeight
                Accessible.name: qsTr("Cerrar configuración")
                onClicked: center.close()
            }
        }
    }

    contentItem: Item {
        // ---- categories (full height, independent scroll)
        Rectangle {
            id: categoryPane
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.px(230)
            color: Theme.panel
            ListView {
                id: categoryList
                objectName: "settingsCategories"
                anchors.fill: parent
                anchors.topMargin: Theme.spacingMd
                clip: true
                model: AppSettings.categories
                keyNavigationEnabled: true
                currentIndex: AppSettings.categories.findIndex(c => c.key === center.category)
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    objectName: "settingsCategory:" + modelData.key
                    width: ListView.view.width
                    height: Theme.rowHeight
                    highlighted: center.category === modelData.key && searchField.text.trim() === ""
                    text: modelData.title + (center.changedIn(modelData.key) > 0 ? "  (" + center.changedIn(modelData.key) + ")" : "")
                    font.pixelSize: Theme.normal
                    Accessible.name: modelData.title
                    onClicked: { searchField.text = ""; center.highlightId = ""; center.category = modelData.key }
                }
            }
        }
        Rectangle {
            id: categoryDivider
            anchors { left: categoryPane.right; top: parent.top; bottom: parent.bottom }
            width: 1
            color: Theme.border
        }

        // ---- page or search results
        StackLayout {
            anchors { left: categoryDivider.right; right: parent.right; top: parent.top; bottom: parent.bottom }
            currentIndex: searchField.text.trim().length > 0 ? 1 : 0

            Flickable {
                id: pageFlick
                objectName: "settingsPage"
                clip: true
                contentHeight: pageColumn.implicitHeight + Theme.spacingXl * 2
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: pageColumn
                    x: Theme.spacingXl
                    y: Theme.spacingXl
                    width: pageFlick.width - Theme.spacingXl * 2
                    spacing: Theme.spacingMd
                    Text {
                        text: center.categoryTitle(center.category)
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.heading
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: AppSettings.loadIssues.length > 0 && center.category === "general"
                        text: "⚠ " + qsTr("Al iniciar se ignoraron valores guardados no válidos:") + "\n• " + AppSettings.loadIssues.join("\n• ")
                        color: Theme.warn
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        id: pageRepeater
                        model: AppSettings.settingsOf(center.category)
                        delegate: SettingControl {
                            required property var modelData
                            Layout.fillWidth: true
                            setting: modelData
                            highlighted: center.highlightId === modelData.id
                            panel: center.panels[modelData.id] ?? null
                            activeFocusOnTab: true
                        }
                    }
                }
            }

            ListView {
                id: resultsView
                objectName: "settingsResults"
                clip: true
                model: AppSettings.search(searchField.text)
                keyNavigationEnabled: true
                spacing: 2
                ScrollBar.vertical: ScrollBar {}
                header: Text {
                    width: resultsView.width
                    padding: Theme.spacingLg
                    text: resultsView.count === 0 ? qsTr("Ningún ajuste coincide con «%1». Solo se listan ajustes que existen.").arg(searchField.text)
                                                  : qsTr("%1 ajustes").arg(resultsView.count)
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
                delegate: ItemDelegate {
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    Keys.onReturnPressed: clicked()
                    onClicked: center.openAt(modelData.category, modelData.id)
                    contentItem: ColumnLayout {
                        spacing: 1
                        Text {
                            text: resultRow.modelData.categoryTitle + "  ›  " + resultRow.modelData.title
                            color: resultRow.highlighted ? Theme.onAccent : Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                        }
                        Text {
                            Layout.fillWidth: true
                            text: resultRow.modelData.description
                            color: resultRow.highlighted ? Theme.onAccent : Theme.textDim
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.toolbarHeight
        color: Theme.header
        border.color: Theme.borderSubtle
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingXl
            anchors.rightMargin: Theme.spacingLg
            spacing: Theme.spacingMd
            Text {
                Layout.fillWidth: true
                text: AppSettings.readOnly ? qsTr("La configuración guardada es de una versión más nueva: los cambios valen solo para esta sesión.")
                                           : qsTr("Cambios respecto a los predeterminados: %1").arg(AppSettings.changedCount)
                color: AppSettings.readOnly ? Theme.warn : Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }
            Button {
                objectName: "resetCategoryButton"
                text: qsTr("Restablecer esta categoría")
                enabled: center.changedIn(center.category) > 0
                onClicked: confirm.ask(qsTr("¿Restablecer «%1»?").arg(center.categoryTitle(center.category)),
                                       qsTr("Solo los ajustes de esta categoría vuelven a sus valores predeterminados."),
                                       () => AppSettings.resetCategory(center.category))
            }
            Button {
                text: qsTr("Restablecer interfaz")
                onClicked: confirm.ask(qsTr("¿Restablecer la interfaz?"),
                                       qsTr("Tema, acento, escala, texto, densidad y fuente vuelven a los predeterminados. Las distribuciones, los proyectos y los mods no cambian."),
                                       () => AppSettings.resetInterface())
            }
            Button {
                text: qsTr("Restablecer todo…")
                onClicked: confirm.ask(qsTr("¿Restablecer todas las preferencias?"),
                                       qsTr("Todos los ajustes y las distribuciones de los tres espacios vuelven a los predeterminados. No se borran proyectos, mods, cachés ni evidencias."),
                                       () => AppSettings.resetAll())
            }
            Button {
                text: qsTr("Cerrar")
                highlighted: true
                onClicked: center.close()
            }
        }
    }

    // ---- confirmation of the resets and of clearing portraits
    Dialog {
        id: confirm
        property var action: null
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.px(460), center.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        function ask(heading, text, action) {
            confirm.title = heading
            confirmText.text = text
            confirm.action = action
            confirm.open()
        }
        Text { id: confirmText; width: parent.width; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: Theme.normal }
        onAccepted: if (action) action()
    }

    // ---- page widgets of the "panel" settings (each one wired to a real backend)
    readonly property var panels: ({
        "general.advancedMode": advancedPanel,
        "general.settingsTransfer": transferPanel,
        "general.settingsDiff": diffPanel,
        "workspace.panels": panelsPanel,
        "workspace.builtinLayouts": builtinPanel,
        "workspace.userLayouts": userLayoutsPanel,
        "npc.portraitCache": portraitCachePanel,
        "performance.portraitStats": portraitStatsPanel,
        "mo2.status": mo2Panel,
        "system.info": systemPanel
    })

    Component {
        id: advancedPanel
        Switch {
            text: checked ? qsTr("Activado") : qsTr("Desactivado")
            checked: Theme.advancedMode
            onToggled: Theme.advancedMode = checked
        }
    }

    Component {
        id: transferPanel
        ColumnLayout {
            id: transferBox
            spacing: Theme.spacingSm
            property var pending: null
            property url pendingFile
            property string message: ""
            RowLayout {
                Button { text: qsTr("Exportar configuración…"); onClicked: exportDialog.open() }
                Button { text: qsTr("Importar configuración…"); onClicked: importDialog.open() }
            }
            Text {
                Layout.fillWidth: true
                visible: transferBox.message.length > 0
                text: transferBox.message
                color: Theme.textDim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: transferBox.pending ? transferBox.pending.changes : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "• " + modelData.title + (modelData.from ? ": " + modelData.from + " → " + modelData.to : ": " + modelData.to)
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                visible: transferBox.pending !== null && transferBox.pending.ok && transferBox.pending.changes.length > 0
                Button {
                    text: qsTr("Aplicar estos cambios")
                    highlighted: true
                    onClicked: {
                        const r = AppSettings.applyImport(transferBox.pendingFile)
                        transferBox.message = r.ok ? qsTr("Importados %1 cambios.").arg(r.applied) : "⚠ " + r.error
                        transferBox.pending = null
                    }
                }
                Button { text: qsTr("Cancelar"); onClicked: { transferBox.pending = null; transferBox.message = qsTr("Importación cancelada; nada cambió.") } }
            }
            FileDialog {
                id: exportDialog
                title: qsTr("Exportar configuración")
                fileMode: FileDialog.SaveFile
                nameFilters: [qsTr("Configuración JSON (*.json)")]
                defaultSuffix: "json"
                onAccepted: {
                    const error = AppSettings.exportTo(selectedFile)
                    transferBox.message = error.length === 0 ? qsTr("Exportada (sin rutas ni credenciales).") : "⚠ " + error
                }
            }
            FileDialog {
                id: importDialog
                title: qsTr("Importar configuración")
                nameFilters: [qsTr("Configuración JSON (*.json)")]
                onAccepted: {
                    transferBox.pendingFile = selectedFile
                    transferBox.pending = AppSettings.previewImport(selectedFile)
                    transferBox.message = !transferBox.pending.ok ? "⚠ " + qsTr("Rechazado: %1. No se cambió nada.").arg(transferBox.pending.error)
                                        : transferBox.pending.changes.length === 0 ? qsTr("El archivo coincide con la configuración actual.")
                                        : qsTr("Cambios que se aplicarían:")
                }
            }
        }
    }

    Component {
        id: diffPanel
        ColumnLayout {
            id: diffBox
            spacing: 2
            readonly property var rows: { AppSettings.revision; return AppSettings.diffFromDefaults() }
            Text {
                visible: diffBox.rows.length === 0
                text: qsTr("Todo está en sus valores predeterminados.")
                color: Theme.ok
                font.pixelSize: Theme.small
            }
            Repeater {
                model: diffBox.rows
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: modelData.categoryTitle + " › " + modelData.title + ": " + modelData.current + "  (" + qsTr("predeterminado") + ": " + modelData.default + ")"
                        color: Theme.text
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                    }
                    ToolButton { text: "↺"; Accessible.name: qsTr("Restablecer"); onClicked: AppSettings.resetSetting(modelData.id) }
                }
            }
        }
    }

    Component {
        id: panelsPanel
        ColumnLayout {
            id: panelsBox
            spacing: Theme.spacingSm
            readonly property var keys: ["creator", "outfit", "forge"]
            property int workspaceIndex: Workbench.workspace
            readonly property WorkspaceLayout layout: Workbench.layout(keys[workspaceIndex])
            ComboBox {
                objectName: "settingsLayoutWorkspace"
                Layout.preferredWidth: Theme.px(280)
                model: [qsTr("Personalizar personaje"), qsTr("Estudio de atuendos"), qsTr("Character Forge")]
                currentIndex: panelsBox.workspaceIndex
                onActivated: panelsBox.workspaceIndex = currentIndex
            }
            GridLayout {
                columns: 3
                columnSpacing: Theme.spacingLg
                CheckBox { text: qsTr("Panel izquierdo"); checked: panelsBox.layout.leftVisible; onToggled: panelsBox.layout.leftVisible = checked }
                Text { text: qsTr("Ancho"); color: Theme.textDim; font.pixelSize: Theme.small }
                SpinBox {
                    objectName: "settingsLeftWidth"
                    from: 160; to: 900; stepSize: 10; editable: true
                    value: panelsBox.layout.leftWidth
                    onValueModified: panelsBox.layout.leftWidth = value
                }
                CheckBox { text: qsTr("Panel derecho"); checked: panelsBox.layout.rightVisible; onToggled: panelsBox.layout.rightVisible = checked }
                Text { text: qsTr("Ancho"); color: Theme.textDim; font.pixelSize: Theme.small }
                SpinBox {
                    from: 200; to: 900; stepSize: 10; editable: true
                    value: panelsBox.layout.rightWidth
                    onValueModified: panelsBox.layout.rightWidth = value
                }
                CheckBox { text: qsTr("Consola"); checked: panelsBox.layout.bottomVisible; onToggled: panelsBox.layout.bottomVisible = checked }
                Text { text: qsTr("Alto"); color: Theme.textDim; font.pixelSize: Theme.small }
                SpinBox {
                    from: 80; to: 700; stepSize: 10; editable: true
                    value: panelsBox.layout.bottomHeight
                    onValueModified: panelsBox.layout.bottomHeight = value
                }
            }
            RowLayout {
                CheckBox { text: qsTr("Visor enfocado (F11)"); checked: panelsBox.layout.maximized; onToggled: panelsBox.layout.maximized = checked }
                Button { text: qsTr("Restablecer este espacio"); onClicked: panelsBox.layout.reset() }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Tamaños a 100 % de escala: se multiplican por la escala de interfaz. El cambio se ve al instante en la ventana.")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: builtinPanel
        Flow {
            spacing: Theme.spacingSm
            Repeater {
                model: Workbench.builtinLayouts
                delegate: Button {
                    required property var modelData
                    text: modelData.name
                    onClicked: Workbench.applyBuiltinLayout(modelData.key, ["creator", "outfit", "forge"][Workbench.workspace])
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Se aplica al espacio de trabajo actual")
                }
            }
        }
    }

    Component {
        id: userLayoutsPanel
        ColumnLayout {
            id: userBox
            spacing: Theme.spacingSm
            property string message: ""
            RowLayout {
                TextField { id: layoutName; Layout.preferredWidth: Theme.px(240); placeholderText: qsTr("Nombre de la distribución"); maximumLength: 40 }
                Button {
                    text: qsTr("Guardar actual")
                    enabled: layoutName.text.trim().length > 0
                    onClicked: {
                        const ok = Workbench.saveLayout(layoutName.text)
                        userBox.message = ok ? qsTr("Guardada «%1».").arg(layoutName.text.trim()) : qsTr("Nombre no válido o reservado para una distribución predefinida.")
                        if (ok) layoutName.text = ""
                    }
                }
            }
            Text { visible: userBox.message.length > 0; text: userBox.message; color: Theme.textDim; font.pixelSize: Theme.small }
            Text { visible: Workbench.savedLayouts.length === 0; text: qsTr("No hay distribuciones guardadas."); color: Theme.textMuted; font.pixelSize: Theme.small }
            Repeater {
                model: Workbench.savedLayouts
                delegate: RowLayout {
                    required property string modelData
                    Text { Layout.fillWidth: true; text: modelData; color: Theme.text; font.pixelSize: Theme.normal }
                    Button { text: qsTr("Aplicar"); onClicked: Workbench.applyLayout(modelData) }
                    Button { text: qsTr("Borrar"); onClicked: confirm.ask(qsTr("¿Borrar «%1»?").arg(modelData), qsTr("Solo se borra esta distribución guardada."), () => Workbench.deleteLayout(modelData)) }
                }
            }
        }
    }

    Component {
        id: portraitCachePanel
        ColumnLayout {
            id: cacheBox
            spacing: Theme.spacingSm
            property string message: ""
            readonly property var usage: center.studio.portraits.cacheUsage(center.studio.portraits.revision)
            Text {
                Layout.fillWidth: true
                text: !cacheBox.usage.available ? qsTr("Sin perfil cargado: no hay caché de retratos activa.")
                      : qsTr("%1 retratos del estudio · %2 capturas de Skyrim · %3 archivos · %4\nVerificados en esta sesión: %5 válidos, %6 desactualizados · %7 listos para mostrarse al instante")
                        .arg(cacheBox.usage.renders).arg(cacheBox.usage.captures).arg(cacheBox.usage.files).arg(center.formatBytes(cacheBox.usage.bytes))
                        .arg(cacheBox.usage.verified).arg(cacheBox.usage.outdated).arg(cacheBox.usage.indexed)
                color: Theme.text
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
            TextEdit {
                Layout.fillWidth: true
                visible: cacheBox.usage.available === true
                readOnly: true
                selectByMouse: true
                text: cacheBox.usage.directory || ""
                color: Theme.textDim
                font.family: Theme.mono
                font.pixelSize: Theme.micro
                wrapMode: TextEdit.WrapAnywhere
            }
            Button {
                objectName: "clearPortraitsButton"
                text: qsTr("Vaciar retratos de este perfil…")
                enabled: cacheBox.usage.available === true && cacheBox.usage.renders > 0
                onClicked: confirm.ask(qsTr("¿Vaciar la caché de retratos?"),
                                       qsTr("Se borrarán %1 retratos del estudio de este perfil (%2). Las capturas de Skyrim que adjuntaste se conservan. Los retratos se volverán a generar al verlos.")
                                           .arg(cacheBox.usage.renders).arg(center.formatBytes(cacheBox.usage.bytes)),
                                       () => { const e = center.studio.portraits.clearRenders(); cacheBox.message = e.length === 0 ? qsTr("Caché vaciada.") : "⚠ " + e })
            }
            Button {
                objectName: "inspectPortraitsButton"
                text: qsTr("Inspeccionar la caché…")
                enabled: cacheBox.usage.available === true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Qué retratos siguen vigentes, cuáles caducaron y por qué; quita solo lo inservible")
                onClicked: cacheInspector.open()
            }
            Text { visible: cacheBox.message.length > 0; text: cacheBox.message; color: Theme.textDim; font.pixelSize: Theme.small }
            PortraitCacheInspector {
                id: cacheInspector
                studio: center.studio
                onRemoveUnusableRequested: confirm.ask(qsTr("¿Quitar lo inservible de la caché?"),
                    qsTr("Se borrarán los retratos desactualizados o dañados y las imágenes sueltas del estudio (≈ %1). Los retratos vigentes, los anteriores y tus capturas de Skyrim se conservan; lo borrado se vuelve a generar al verlo.")
                        .arg(cacheInspector.bytesText(cacheInspector.report.unusableBytes ?? 0)),
                    () => center.studio.portraits.removeUnusable())
                onClearVariantsRequested: confirm.ask(qsTr("¿Vaciar las imágenes del A/B?"),
                    qsTr("Se borran solo las imágenes de la comparación de fuentes de apariencia; se vuelven a dibujar al abrir una comparación."),
                    () => { const e = center.studio.portraits.clearVariants(); cacheBox.message = e.length === 0 ? qsTr("Imágenes del A/B quitadas.") : "⚠ " + e })
            }
        }
    }

    Component {
        id: portraitStatsPanel
        GridLayout {
            id: statsBox
            columns: 2
            columnSpacing: Theme.spacingXl
            readonly property var s: center.studio.portraits.stats
            Repeater {
                model: [
                    [qsTr("Pedidos por tarjetas"), "requested"], [qsTr("Aciertos de caché"), "cacheHits"],
                    [qsTr("Mostrados al instante (provisionales)"), "provisional"], [qsTr("Verificados en segundo plano"), "verified"],
                    [qsTr("Lotes de verificación"), "lookupBatches"], [qsTr("Lote más lento (ms)"), "maxLookupBatchMs"],
                    [qsTr("Retratos precargados"), "prefetched"], [qsTr("Generados"), "rendered"],
                    [qsTr("Desactualizados"), "stale"], [qsTr("Errores"), "errors"],
                    [qsTr("Cola visible / render / precarga"), "queues"], [qsTr("Render medio / máximo (ms)"), "times"],
                    [qsTr("Índice de caché (retratos, ms)"), "index"]
                ]
                delegate: Text {
                    required property var modelData
                    text: modelData[0] + ": " + (modelData[1] === "queues" ? (statsBox.s.queue + " / " + statsBox.s.renderQueue + " / " + statsBox.s.prefetchQueue)
                                               : modelData[1] === "times" ? (statsBox.s.averageRenderMs + " / " + statsBox.s.maxRenderMs)
                                               : modelData[1] === "index" ? (statsBox.s.indexed + ", " + statsBox.s.indexMs)
                                               : statsBox.s[modelData[1]])
                    color: Theme.text
                    font.pixelSize: Theme.small
                }
            }
        }
    }

    Component {
        id: mo2Panel
        ColumnLayout {
            id: mo2Box
            spacing: 2
            readonly property var sum: center.studio.summary
            Repeater {
                model: [
                    [qsTr("Instancia"), center.studio.instanceName || qsTr("ninguna")],
                    [qsTr("Perfil"), center.studio.selectedProfile || qsTr("ninguno")],
                    [qsTr("Juego"), (mo2Box.sum.gameVersion || "?") + " · " + (mo2Box.sum.edition || "?") + " · " + (mo2Box.sum.language || "?")],
                    [qsTr("Mods / plugins activos / inactivos"), (mo2Box.sum.mods ?? "?") + " / " + (mo2Box.sum.activePlugins ?? "?") + " / " + (mo2Box.sum.inactivePlugins ?? "?")],
                    [qsTr("Archivos virtuales"), mo2Box.sum.files ?? "?"],
                    [qsTr("NPC / armaduras indexados"), (mo2Box.sum.npcs ?? qsTr("sin indexar")) + " / " + (mo2Box.sum.armors ?? "-")],
                    [qsTr("Último indexado"), mo2Box.sum.indexMs !== undefined ? mo2Box.sum.indexMs + " ms, " + qsTr("%1 plugins desde caché").arg(mo2Box.sum.cachedPlugins) : qsTr("no indexado")]
                ]
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData[0] + ": " + modelData[1]
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Solo lectura. Para cambiar de instancia o perfil: Archivo › Instancia y perfil de MO2. El estudio nunca escribe modlist.txt, plugins.txt ni Skyrim/Data.")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: systemPanel
        ColumnLayout {
            id: sysBox
            spacing: 2
            readonly property var info: AppSettings.systemInfo()
            readonly property string api: GraphicsInfo.api === GraphicsInfo.Direct3D11 ? "Direct3D 11"
                                        : GraphicsInfo.api === GraphicsInfo.Direct3D12 ? "Direct3D 12"
                                        : GraphicsInfo.api === GraphicsInfo.Vulkan ? "Vulkan"
                                        : GraphicsInfo.api === GraphicsInfo.OpenGL ? "OpenGL"
                                        : GraphicsInfo.api === GraphicsInfo.Software ? qsTr("Software") : qsTr("No detectado")
            readonly property string report: [
                "Skyrim NPC Studio " + info.appVersion + " (" + info.buildType + ")",
                "Qt " + info.qtRuntime + " (" + qsTr("compilado con") + " " + info.qtBuild + ")",
                info.os + " · " + info.kernel + " · " + info.cpuArchitecture,
                qsTr("API gráfica") + ": " + api,
                qsTr("Preferencias en") + ": " + info.settingsStorage,
                qsTr("Perfil MO2") + ": " + (center.studio.selectedProfile || "-") + " · NPC " + (center.studio.summary.npcs ?? "-")
            ].join("\n")
            Text { Layout.fillWidth: true; text: sysBox.report; color: Theme.text; font.pixelSize: Theme.small; wrapMode: Text.WordWrap }
            Text {
                Layout.fillWidth: true
                text: qsTr("Los retratos y el visor son renders del estudio: nada de esta página indica una verificación dentro de Skyrim.")
                color: Theme.textMuted
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("Copiar información técnica")
                onClicked: { clip.text = sysBox.report; clip.selectAll(); clip.copy(); copied.visible = true }
            }
            Text { id: copied; visible: false; text: qsTr("Copiado al portapapeles."); color: Theme.ok; font.pixelSize: Theme.small }
            TextEdit { id: clip; visible: false }
        }
    }
}
