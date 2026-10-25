import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Biblioteca visual de packs de ropa sin ESP y proyectos BodySlide (Épica C / RC-A Punto 4).
// Descubre recursos presentes en el VFS de MO2 que no tienen registro ARMO propio o son replacers.
// La equipación directa está deshabilitada (DoD: la conversión a ARMO es para fases posteriores).
Item {
    id: root
    required property StudioController studio
    readonly property LooseResourceLibrary library: studio.looseResources
    signal previewRequested(string dataRelativePath)

    property string activeKindFilter: ""
    property string activeFamilyFilter: ""
    property string activeModFilter: ""

    function search() {
        library.search(searchField.text, activeFamilyFilter, activeModFilter, activeKindFilter)
    }

    Timer { id: debounce; interval: 220; onTriggered: root.search() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingMd

        // ---------------------------------------------------------------- CABECERA Y RESUMEN
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Packs de ropa sin ESP y Proyectos BodySlide")
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: Theme.large
                }
                Text {
                    text: qsTr("Mallas sueltas, replacers y conjuntos BodySlide descubiertos en el VFS de MO2 sin plugin propio.")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Button {
                text: qsTr("Recargar")
                icon.name: "view-refresh"
                implicitHeight: Theme.controlHeight
                enabled: !root.library.indexing
                onClicked: root.library.reload()
            }
        }

        // Resumen de conteos
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight + Theme.spacingSm
            color: Theme.header
            radius: 4
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingLg

                Text {
                    text: qsTr("Total: %1").arg(root.library.totalResources)
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.small
                }
                Text {
                    text: qsTr("Mallas sueltas: %1").arg(root.library.looseMeshCount)
                    color: "#5dade2"
                    font.pixelSize: Theme.small
                }
                Text {
                    text: qsTr("BodySlide: %1").arg(root.library.bodyslideProjectCount)
                    color: "#af7ac5"
                    font.pixelSize: Theme.small
                }
                Text {
                    text: qsTr("Replacers: %1").arg(root.library.replacerMeshCount)
                    color: "#f39c12"
                    font.pixelSize: Theme.small
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: root.library.indexing
                    visible: running
                    implicitWidth: 20
                    implicitHeight: 20
                }
            }
        }

        // ---------------------------------------------------------------- FILTROS DE BÚSQUEDA
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar por nombre, ruta o mod proveedor…")
                onTextChanged: debounce.restart()
            }

            ComboBox {
                id: kindBox
                implicitWidth: Theme.px(160)
                model: [
                    { text: qsTr("Todos los tipos"), value: "" },
                    { text: qsTr("Mallas sueltas"), value: "loose-mesh" },
                    { text: qsTr("Proyectos BodySlide"), value: "bodyslide-project" },
                    { text: qsTr("Replacers"), value: "replacer-mesh" }
                ]
                textRole: "text"
                valueRole: "value"
                onActivated: {
                    root.activeKindFilter = currentValue
                    root.search()
                }
            }

            ComboBox {
                id: familyBox
                implicitWidth: Theme.px(140)
                model: root.library.availableFamilies
                textRole: "text"
                valueRole: "value"
                onActivated: {
                    root.activeFamilyFilter = currentValue
                    root.search()
                }
            }

            ComboBox {
                id: modBox
                implicitWidth: Theme.px(170)
                model: root.library.availableMods
                textRole: "text"
                valueRole: "value"
                onActivated: {
                    root.activeModFilter = currentValue
                    root.search()
                }
            }
        }

        // ---------------------------------------------------------------- LISTA DE RECURSOS
        ListView {
            id: resourceListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.library.resources
            spacing: Theme.spacingSm

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: card
                width: resourceListView.width
                implicitHeight: cardContent.implicitHeight + Theme.spacingMd * 2
                color: root.library.selectedResource.resourceId === modelData.resourceId
                       ? Theme.selection
                       : (mouseArea.containsMouse ? Theme.surface : Theme.panel)
                radius: 4
                border.color: root.library.selectedResource.resourceId === modelData.resourceId
                              ? Theme.accent
                              : Theme.border
                border.width: 1

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.library.selectResource(modelData.resourceId)
                    onDoubleClicked: {
                        root.library.selectResource(modelData.resourceId)
                        if (modelData.capabilities && modelData.capabilities.previewable) {
                            root.previewRequested(modelData.dataRelativePath)
                        }
                    }
                }

                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingXs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: modelData.displayName
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        // Badge Tipo
                        Rectangle {
                            implicitHeight: 20
                            implicitWidth: kindText.implicitWidth + 12
                            radius: 3
                            color: modelData.kind === "bodyslide-project" ? "#4a235a" :
                                   modelData.kind === "replacer-mesh" ? "#7e5109" : "#1b4f72"
                            Text {
                                id: kindText
                                anchors.centerIn: parent
                                text: modelData.kindLabel
                                color: "#ffffff"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        // Badge Familia Corporal
                        Rectangle {
                            visible: Boolean(modelData.bodyFamily && modelData.bodyFamily !== "unknown")
                            implicitHeight: 20
                            implicitWidth: famText.implicitWidth + 12
                            radius: 3
                            color: "#1e8449"
                            Text {
                                id: famText
                                anchors.centerIn: parent
                                text: modelData.bodyFamily
                                color: "#ffffff"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        // Badge Categoría
                        Rectangle {
                            implicitHeight: 20
                            implicitWidth: catText.implicitWidth + 12
                            radius: 3
                            color: Theme.header
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                id: catText
                                anchors.centerIn: parent
                                text: modelData.categoryLabel
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                    }

                    // Proveedor y ruta
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            text: qsTr("Mod MO2:")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                        }
                        Text {
                            text: modelData.winningProvider
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: Boolean(modelData.sizeBytes > 0)
                            text: (modelData.sizeBytes / 1024).toFixed(1) + " KB"
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: modelData.dataRelativePath
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    // Advertencias / Issues
                    Repeater {
                        model: modelData.issues
                        Text {
                            text: "⚠️ " + modelData
                            color: Theme.warn
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // Acciones
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        spacing: Theme.spacingSm

                        Button {
                            text: qsTr("👁️ Previsualizar")
                            enabled: modelData.capabilities && modelData.capabilities.previewable
                            implicitHeight: Theme.controlHeight - 4
                            onClicked: {
                                root.library.selectResource(modelData.resourceId)
                                root.previewRequested(modelData.dataRelativePath)
                            }
                            ToolTip.visible: hovered && !enabled
                            ToolTip.text: qsTr("No previsualizable directamente (requiere construcción previa)")
                        }

                        Button {
                            text: qsTr("🔍 Detalles técnicos")
                            implicitHeight: Theme.controlHeight - 4
                            onClicked: {
                                root.library.selectResource(modelData.resourceId)
                                detailsDialog.open()
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Botón Equipar DESHABILITADO con ToolTip explicativo (DoD / RC-A)
                        Button {
                            text: qsTr("Equipar")
                            enabled: false
                            implicitHeight: Theme.controlHeight - 4
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Recurso sin plugin. Para equipar se requiere crear un registro ARMO en una versión posterior.")
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- DIÁLOGO DE DETALLES TÉCNICOS
    Dialog {
        id: detailsDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.px(540), Overlay.overlay ? Overlay.overlay.width - 40 : Theme.px(540))
        title: qsTr("Detalles del recurso:") + " " + (root.library.selectedResource.displayName || "")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSm

            KeyValue {
                label: qsTr("ID de recurso")
                value: root.library.selectedResource.resourceId || "—"
            }
            KeyValue {
                label: qsTr("Tipo de recurso")
                value: root.library.selectedResource.kindLabel || "—"
            }
            KeyValue {
                label: qsTr("Ruta en Data")
                value: root.library.selectedResource.dataRelativePath || "—"
            }
            KeyValue {
                label: qsTr("Mod ganador")
                value: root.library.selectedResource.winningProvider || "—"
            }
            KeyValue {
                label: qsTr("Familia corporal")
                value: root.library.selectedResource.bodyFamily || "—"
            }
            KeyValue {
                label: qsTr("Categoría")
                value: root.library.selectedResource.categoryLabel || "—"
            }

            // Asociación ARMO (si es replacer)
            Rectangle {
                visible: Boolean(root.library.selectedResource && root.library.selectedResource.association && root.library.selectedResource.association.armorFormKey)
                Layout.fillWidth: true
                implicitHeight: assocCol.implicitHeight + Theme.spacingSm * 2
                color: Theme.header
                radius: 3
                ColumnLayout {
                    id: assocCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    Text {
                        text: qsTr("Asociación con equipamiento existente:")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.small
                    }
                    Text {
                        text: qsTr("FormKey ARMO: %1").arg(root.library.selectedResource.association ? root.library.selectedResource.association.armorFormKey : "")
                        color: Theme.text
                        font.pixelSize: Theme.small
                    }
                    Text {
                        text: qsTr("Confianza: %1").arg(root.library.selectedResource.association ? root.library.selectedResource.association.confidence : "")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                    }
                }
            }

            // Archivos asociados
            Text {
                text: qsTr("Archivos asociados (%1):").arg(root.library.selectedResource.associatedFiles ? root.library.selectedResource.associatedFiles.length : 0)
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.small
                Layout.topMargin: Theme.spacingSm
            }
            ListView {
                Layout.fillWidth: true
                implicitHeight: Math.min(Theme.px(100), contentHeight)
                clip: true
                model: root.library.selectedResource.associatedFiles || []
                delegate: Text {
                    text: "• " + modelData
                    color: Theme.textDim
                    font.pixelSize: 11
                    font.family: "Consolas"
                    elide: Text.ElideMiddle
                    width: parent.width
                }
            }

            // Diagnósticos
            Text {
                visible: Boolean(root.library.selectedResource && root.library.selectedResource.issues && root.library.selectedResource.issues.length > 0)
                text: qsTr("Diagnósticos:")
                color: Theme.warn
                font.bold: true
                font.pixelSize: Theme.small
                Layout.topMargin: Theme.spacingSm
            }
            ListView {
                visible: Boolean(root.library.selectedResource && root.library.selectedResource.issues && root.library.selectedResource.issues.length > 0)
                Layout.fillWidth: true
                implicitHeight: Math.min(Theme.px(80), contentHeight)
                clip: true
                model: root.library.selectedResource.issues || []
                delegate: Text {
                    text: "⚠️ " + modelData
                    color: Theme.warn
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
            }
        }
    }
}
