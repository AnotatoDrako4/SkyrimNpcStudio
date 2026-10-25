import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    readonly property int presetsCount: (root.studio.appearance.presetsList || []).length

    Component.onCompleted: {
        root.ScrollBar.vertical.Accessible.name = qsTr("Desplazar el panel de presets")
        root.ScrollBar.horizontal.Accessible.name = qsTr("Desplazar el panel de presets en horizontal")
    }

    FileDialog {
        id: jslotFileDialog
        title: qsTr("Seleccionar Preset RaceMenu (.jslot)")
        nameFilters: ["RaceMenu Presets (*.jslot)"]
        onAccepted: {
            var path = selectedFile.toString().replace(/^file:\/\/\//, "")
            var comp = root.studio.appearance.evaluatePresetCompatibility(path)
            if (!comp.compatible) {
                compatDialog.issues = comp.issues
                compatDialog.pendingPath = path
                compatDialog.open()
            } else {
                root.studio.appearance.applyJslotPreset(path)
            }
        }
    }

    PresetHeadDialog {
        id: headDialog
        studio: root.studio
    }

    Dialog {
        id: compatDialog
        title: qsTr("Informe de Compatibilidad de Preset")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Theme.px(480)
        property var issues: []
        property string pendingPath: ""
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: Theme.sp(8)
            width: parent.width

            RowLayout {
                spacing: Theme.sp(8)
                Icon {
                    name: "warning"
                    size: Theme.heading
                    color: Theme.warn
                }
                Text {
                    text: qsTr("Se detectaron advertencias o incompatibilidades en el preset:")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.normal
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            Repeater {
                model: compatDialog.issues
                delegate: Text {
                    text: "• " + modelData
                    color: Theme.warn
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.pixelSize: Theme.small
                }
            }

            Text {
                text: qsTr("¿Deseas aplicar el preset de todos modos?")
                color: Theme.textDim
                font.pixelSize: Theme.small
                Layout.topMargin: Theme.sp(6)
            }
        }
        onAccepted: {
            if (pendingPath.length > 0) root.studio.appearance.applyJslotPreset(pendingPath)
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.sp(16)
        Layout.margins: Theme.sp(12)

        // =========================================================================
        // CARD 1: PRESETS DE RACEMENU (.jslot)
        // =========================================================================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: presetsCardCol.implicitHeight + Theme.sp(24)
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusLarge

            ColumnLayout {
                id: presetsCardCol
                anchors.fill: parent
                anchors.margins: Theme.sp(12)
                spacing: Theme.sp(10)

                // Encabezado de la tarjeta con badge de conteo y acciones
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(8)

                    Rectangle {
                        width: Theme.px(30); height: Theme.px(30)
                        radius: Theme.radiusSmall
                        color: Theme.accentSubtle
                        border.color: Theme.accentBorder
                        border.width: 1
                        Icon {
                            anchors.centerIn: parent
                            name: "document"
                            color: Theme.accent
                            size: Theme.normal
                        }
                    }

                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true

                        RowLayout {
                            spacing: Theme.sp(8)
                            Text {
                                text: qsTr("Presets de RaceMenu (.jslot)")
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Theme.large
                            }

                            Rectangle {
                                visible: root.presetsCount > 0
                                height: Theme.px(18)
                                radius: 9
                                color: Theme.accentSubtle
                                border.color: Theme.accentBorder
                                border.width: 1
                                implicitWidth: countText.implicitWidth + Theme.sp(10)
                                Text {
                                    id: countText
                                    anchors.centerIn: parent
                                    text: qsTr("%1").arg(root.presetsCount)
                                    color: Theme.accent
                                    font.pixelSize: Theme.micro
                                    font.bold: true
                                }
                            }
                        }

                        Text {
                            text: qsTr("Carga morphs y esculturas faciales exportadas desde RaceMenu.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    IconToolButton {
                        glyph: "refresh"
                        label: qsTr("Escanear")
                        showLabel: root.width >= Theme.px(380)
                        tip: qsTr("Escanear presets en mods instalados (SKSE\\Plugins\\CharGen\\Presets)")
                        onClicked: root.studio.appearance.loadPresetsFromVfs()
                    }

                    IconToolButton {
                        glyph: "folder-open"
                        label: qsTr("Importar…")
                        tip: qsTr("Seleccionar un archivo .jslot en el disco")
                        emphasized: true
                        onClicked: jslotFileDialog.open()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

                // Estado vacío cuando no hay presets
                Rectangle {
                    visible: root.presetsCount === 0
                    Layout.fillWidth: true
                    implicitHeight: emptyCol.implicitHeight + Theme.sp(24)
                    color: Theme.surface
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radius

                    ColumnLayout {
                        id: emptyCol
                        anchors.centerIn: parent
                        width: parent.width - Theme.sp(24)
                        spacing: Theme.sp(6)

                        Icon {
                            name: "folder"
                            size: Theme.heading
                            color: Theme.textMuted
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("No hay presets .jslot detectados")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Text {
                            text: qsTr("Haz clic en 'Escanear' para buscar en tus mods o en 'Importar' para abrir un archivo.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Lista de presets encontrados
                ListView {
                    visible: root.presetsCount > 0
                    Layout.fillWidth: true
                    implicitHeight: Math.min(Theme.px(220), root.presetsCount * Theme.px(48))
                    clip: true
                    model: root.studio.appearance.presetsList
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: Theme.sp(4)

                    delegate: Rectangle {
                        id: presetRow
                        width: ListView.view.width
                        implicitHeight: Theme.px(44)
                        radius: Theme.radius
                        color: presetHover.hovered ? Theme.panelAlt : Theme.control
                        border.color: presetHover.hovered ? Theme.accentBorder : Theme.border
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 100 } }
                        Behavior on border.color { ColorAnimation { duration: 100 } }
                        HoverHandler { id: presetHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp(6)
                            spacing: Theme.sp(8)

                            Rectangle {
                                width: Theme.px(24); height: Theme.px(24)
                                radius: 2
                                color: Theme.panel
                                border.color: Theme.borderSubtle
                                Icon {
                                    anchors.centerIn: parent
                                    name: "person"
                                    size: Theme.small
                                    color: Theme.accent
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Text {
                                    text: modelData.name
                                    color: Theme.text
                                    font.bold: true
                                    font.pixelSize: Theme.normal
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: (modelData.sourceMod ? modelData.sourceMod + " · " : "") + modelData.fileName
                                    color: Theme.textDim
                                    font.pixelSize: Theme.micro
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                objectName: "presetHeadButton"
                                text: qsTr("Export Head…")
                                implicitHeight: Theme.controlHeight - Theme.sp(6)
                                Accessible.name: qsTr("Comprobar la cabeza exportada de %1").arg(modelData.name)
                                onClicked: {
                                    root.studio.presetHeads.check(modelData.diskPath)
                                    headDialog.open()
                                }
                            }

                            Button {
                                text: qsTr("Aplicar")
                                highlighted: true
                                implicitHeight: Theme.controlHeight - Theme.sp(6)
                                onClicked: {
                                    var p = modelData.diskPath
                                    var comp = root.studio.appearance.evaluatePresetCompatibility(p)
                                    if (!comp.compatible) {
                                        compatDialog.issues = comp.issues
                                        compatDialog.pendingPath = p
                                        compatDialog.open()
                                    } else {
                                        root.studio.appearance.applyJslotPreset(p)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // =========================================================================
        // CARD 2: TRANSFERENCIA DE APARIENCIA (NPC DONANTE / REPLACER)
        // =========================================================================
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: donorCardCol.implicitHeight + Theme.sp(24)
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusLarge

            ColumnLayout {
                id: donorCardCol
                anchors.fill: parent
                anchors.margins: Theme.sp(12)
                spacing: Theme.sp(10)

                // Encabezado de Donantes
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(8)

                    Rectangle {
                        width: Theme.px(30); height: Theme.px(30)
                        radius: Theme.radiusSmall
                        color: Theme.accentSubtle
                        border.color: Theme.accentBorder
                        border.width: 1
                        Icon {
                            anchors.centerIn: parent
                            name: "people"
                            color: Theme.accent
                            size: Theme.normal
                        }
                    }

                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Transferencia de Apariencia (NPC Donante)")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.large
                        }

                        Text {
                            text: qsTr("Aplica el rostro de otro NPC (morphs, partes y tintes). El FaceGen se regenera limpiamente.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Alerta de restricción si el donante está bloqueado
                Rectangle {
                    visible: root.studio.appearance.donorBlockedReason.length > 0
                    Layout.fillWidth: true
                    implicitHeight: warnRow.implicitHeight + Theme.sp(10)
                    color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
                    border.color: Theme.warn
                    border.width: 1
                    radius: Theme.radius

                    RowLayout {
                        id: warnRow
                        anchors.fill: parent
                        anchors.margins: Theme.sp(8)
                        spacing: Theme.sp(8)

                        Icon {
                            name: "warning"
                            color: Theme.warn
                            size: Theme.normal
                        }

                        Text {
                            text: qsTr("Operación restringida: %1").arg(root.studio.appearance.donorBlockedReason)
                            color: Theme.warn
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Barra de búsqueda integrada
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(8)

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.controlHeight
                        radius: Theme.radius
                        color: Theme.control
                        border.color: donorSearchField.activeFocus ? Theme.borderFocus : Theme.border
                        border.width: donorSearchField.activeFocus ? Theme.focusWidth : 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.sp(8)
                            anchors.rightMargin: Theme.sp(6)
                            spacing: Theme.sp(6)

                            Icon {
                                name: "search"
                                color: donorSearchField.activeFocus ? Theme.accent : Theme.textMuted
                                size: Theme.normal
                            }

                            TextField {
                                id: donorSearchField
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                background: null
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.normal
                                placeholderTextColor: Theme.textMuted
                                Accessible.name: qsTr("Buscar NPC donante")
                                placeholderText: qsTr("Buscar por nombre (ej. Lydia, Serana, Aela)...")
                                onAccepted: root.studio.searchDonors(text)
                            }

                            ToolButton {
                                visible: donorSearchField.text.length > 0
                                implicitWidth: Theme.px(24)
                                implicitHeight: Theme.px(24)
                                contentItem: Icon {
                                    name: "clear"
                                    size: Theme.small
                                    color: parent.hovered ? Theme.text : Theme.textDim
                                }
                                background: null
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Limpiar búsqueda")
                                onClicked: {
                                    donorSearchField.text = ""
                                    root.studio.searchDonors("")
                                }
                            }
                        }
                    }

                    Button {
                        implicitHeight: Theme.controlHeight
                        highlighted: true
                        Accessible.name: qsTr("Buscar donante")
                        contentItem: RowLayout {
                            spacing: Theme.sp(6)
                            Icon {
                                name: "search"
                                size: Theme.small
                                color: Theme.onAccent
                            }
                            Text {
                                text: qsTr("Buscar")
                                color: Theme.onAccent
                                font.bold: true
                                font.pixelSize: Theme.normal
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radius
                            color: parent.down ? Theme.accentPressed : parent.hovered ? Theme.accentHover : Theme.accent
                        }
                        onClicked: root.studio.searchDonors(donorSearchField.text)
                    }
                }

                // Selector de modo de vista (Segmented Control con persistencia de tests)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    radius: Theme.radius
                    color: Theme.control
                    border.color: Theme.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 2
                        spacing: 2

                        Button {
                            id: donorListBtn
                            objectName: "donorListModeButton"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            checkable: true
                            checked: AppSettings.donorView === "list"
                            onClicked: AppSettings.donorView = "list"
                            Accessible.name: qsTr("Vista de lista")
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: donorListBtn.checked ? Theme.selection : donorListBtn.hovered ? Theme.panelAlt : "transparent"
                                border.color: donorListBtn.checked ? Theme.accentBorder : "transparent"
                                border.width: 1
                            }
                            contentItem: RowLayout {
                                spacing: Theme.sp(6)
                                Item { Layout.fillWidth: true }
                                Icon {
                                    name: "list"
                                    size: Theme.normal
                                    color: donorListBtn.checked ? Theme.accent : Theme.textDim
                                }
                                Text {
                                    text: qsTr("Lista")
                                    color: donorListBtn.checked ? Theme.text : Theme.textDim
                                    font.bold: donorListBtn.checked
                                    font.pixelSize: Theme.normal
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        Button {
                            id: donorGalleryBtn
                            objectName: "donorGalleryModeButton"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            checkable: true
                            checked: AppSettings.donorView === "gallery"
                            onClicked: AppSettings.donorView = "gallery"
                            Accessible.name: qsTr("Galería de rostros")
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: donorGalleryBtn.checked ? Theme.selection : donorGalleryBtn.hovered ? Theme.panelAlt : "transparent"
                                border.color: donorGalleryBtn.checked ? Theme.accentBorder : "transparent"
                                border.width: 1
                            }
                            contentItem: RowLayout {
                                spacing: Theme.sp(6)
                                Item { Layout.fillWidth: true }
                                Icon {
                                    name: "grid"
                                    size: Theme.normal
                                    color: donorGalleryBtn.checked ? Theme.accent : Theme.textDim
                                }
                                Text {
                                    text: qsTr("Galería de rostros")
                                    color: donorGalleryBtn.checked ? Theme.text : Theme.textDim
                                    font.bold: donorGalleryBtn.checked
                                    font.pixelSize: Theme.normal
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                // Estado informativo cuando no hay búsqueda activa
                Rectangle {
                    visible: root.studio.donorResults.count === 0 && donorSearchField.text.length === 0
                    Layout.fillWidth: true
                    implicitHeight: Theme.px(120)
                    color: Theme.surface
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radius

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - Theme.sp(24)
                        spacing: Theme.sp(6)

                        Icon {
                            name: "people"
                            size: Theme.heading
                            color: Theme.textMuted
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("Encuentra y transfiere rostros de NPCs")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("Escribe el nombre de un NPC donante en el buscador para ver sus rostros y transferir su apariencia.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Estado de "sin resultados" tras una búsqueda
                Rectangle {
                    visible: root.studio.donorResults.count === 0 && donorSearchField.text.length > 0
                    Layout.fillWidth: true
                    implicitHeight: Theme.px(100)
                    color: Theme.surface
                    border.color: Theme.borderSubtle
                    border.width: 1
                    radius: Theme.radius

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - Theme.sp(24)
                        spacing: Theme.sp(6)

                        Icon {
                            name: "info"
                            size: Theme.title
                            color: Theme.textMuted
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("No se encontraron NPCs para \"%1\"").arg(donorSearchField.text)
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("Verifica que el nombre esté bien escrito o que el mod esté activo en tu orden de carga.")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Galería de donantes (GridView)
                GridView {
                    id: donorGallery
                    objectName: "donorGallery"
                    visible: AppSettings.donorView === "gallery" && count > 0
                    Layout.fillWidth: true
                    implicitHeight: Math.min(cellHeight * 2.8, Math.ceil(count / columns) * cellHeight)
                    clip: true
                    model: AppSettings.donorView === "gallery" ? root.studio.donorResults : null
                    boundsBehavior: Flickable.StopAtBounds
                    readonly property int columns: Math.max(1, Math.floor(width / AppSettings.galleryCardWidth))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: cellWidth - 10 + 78
                    cacheBuffer: cellHeight
                    ScrollBar.vertical: ScrollBar { Accessible.name: qsTr("Desplazar la galería de donantes") }

                    delegate: Item {
                        id: donorCard
                        required property string formKey
                        required property string name
                        required property string editorId
                        required property string race
                        required property string sex
                        required property string winningPlugin
                        width: donorGallery.cellWidth
                        height: donorGallery.cellHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: Theme.sp(4)
                            radius: Theme.radiusLarge
                            color: donorHover.hovered ? Theme.panelAlt : Theme.surface
                            border.color: donorHover.hovered ? Theme.accentBorder : Theme.border
                            border.width: donorHover.hovered ? 1.5 : 1

                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on border.color { ColorAnimation { duration: 120 } }
                            HoverHandler { id: donorHover }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.sp(6)
                                spacing: Theme.sp(2)

                                PortraitThumb {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: width
                                    portraits: root.studio.portraits
                                    formKey: donorCard.formKey
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: donorCard.name || donorCard.editorId || donorCard.formKey
                                    color: Theme.text
                                    font.bold: true
                                    font.pixelSize: Theme.small
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.sp(4)

                                    Rectangle {
                                        radius: 2
                                        color: Theme.control
                                        implicitWidth: sexText.implicitWidth + 6
                                        implicitHeight: sexText.implicitHeight + 2
                                        Text {
                                            id: sexText
                                            anchors.centerIn: parent
                                            text: donorCard.sex === "female" ? "♀" : donorCard.sex === "male" ? "♂" : "?"
                                            color: donorCard.sex === "female" ? "#f48fb1" : "#90caf9"
                                            font.pixelSize: Theme.micro
                                            font.bold: true
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: (donorCard.race || "?") + " · " + donorCard.winningPlugin
                                        color: Theme.textDim
                                        font.pixelSize: Theme.micro
                                        elide: Text.ElideRight
                                    }
                                }

                                Button {
                                    Layout.fillWidth: true
                                    implicitHeight: Theme.controlHeight - Theme.sp(4)
                                    text: qsTr("Usar")
                                    highlighted: true
                                    font.pixelSize: Theme.small
                                    font.bold: true
                                    enabled: root.studio.appearance.donorBlockedReason.length === 0
                                    ToolTip.visible: hovered
                                    ToolTip.text: enabled ? qsTr("Aplicar el rostro de %1 (se regenera el FaceGen; no se copia el del donante)").arg(donorCard.name || donorCard.editorId)
                                                          : root.studio.appearance.donorBlockedReason
                                    onClicked: root.studio.appearance.applyDonor(donorCard.formKey, donorCard.name || donorCard.editorId)
                                }
                            }
                        }
                    }
                }

                // Lista de donantes (ListView)
                ListView {
                    visible: AppSettings.donorView === "list" && count > 0
                    Layout.fillWidth: true
                    implicitHeight: Math.min(Theme.px(240), count * Theme.px(48))
                    clip: true
                    model: root.studio.donorResults
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: Theme.sp(4)

                    delegate: Rectangle {
                        id: donorListRow
                        width: ListView.view.width
                        implicitHeight: Theme.px(44)
                        radius: Theme.radius
                        color: listHover.hovered ? Theme.panelAlt : index % 2 === 0 ? Theme.control : Theme.panel
                        border.color: listHover.hovered ? Theme.accentBorder : Theme.border
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 100 } }
                        Behavior on border.color { ColorAnimation { duration: 100 } }
                        HoverHandler { id: listHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp(6)
                            spacing: Theme.sp(8)

                            PortraitThumb {
                                Layout.preferredWidth: Theme.px(30)
                                Layout.preferredHeight: Theme.px(30)
                                portraits: root.studio.portraits
                                formKey: model.formKey
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 1

                                Text {
                                    Layout.fillWidth: true
                                    text: model.name || model.editorId || model.formKey
                                    color: Theme.text
                                    font.bold: true
                                    font.pixelSize: Theme.normal
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.sp(4)

                                    Rectangle {
                                        radius: 2
                                        color: Theme.panelHeader
                                        implicitWidth: listSex.implicitWidth + 4
                                        implicitHeight: listSex.implicitHeight + 2
                                        Text {
                                            id: listSex
                                            anchors.centerIn: parent
                                            text: model.sex === "female" ? "♀" : model.sex === "male" ? "♂" : "?"
                                            color: model.sex === "female" ? "#f48fb1" : "#90caf9"
                                            font.pixelSize: Theme.micro
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: (model.race || "") + " · " + (model.winningPlugin || "")
                                        color: Theme.textDim
                                        font.pixelSize: Theme.micro
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Button {
                                text: qsTr("Usar como Donante")
                                highlighted: listHover.hovered
                                implicitHeight: Theme.controlHeight - Theme.sp(6)
                                enabled: root.studio.appearance.donorBlockedReason.length === 0
                                ToolTip.visible: hovered && !enabled
                                ToolTip.text: root.studio.appearance.donorBlockedReason
                                onClicked: {
                                    root.studio.appearance.applyDonor(model.formKey, model.name || model.editorId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
