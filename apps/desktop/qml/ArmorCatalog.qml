import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Catálogo visual y buscador de ropa y armaduras (Creative Workspace).
Item {
    id: root
    required property StudioController studio
    signal previewRequested(string formKey)

    property bool isGrid: false
    property bool showAdvancedFilters: false
    property string activeTypeFilter: ""

    readonly property var slotOptions: [
        { text: qsTr("Todos los slots"), value: 0 },
        { text: "30 · " + qsTr("Cabeza / Casco"), value: 30 },
        { text: "31 · " + qsTr("Cabello"), value: 31 },
        { text: "32 · " + qsTr("Cuerpo / Pechera"), value: 32 },
        { text: "33 · " + qsTr("Manos / Guanteletes"), value: 33 },
        { text: "34 · " + qsTr("Antebrazos"), value: 34 },
        { text: "35 · " + qsTr("Amuleto"), value: 35 },
        { text: "36 · " + qsTr("Anillo"), value: 36 },
        { text: "37 · " + qsTr("Pies / Botas"), value: 37 },
        { text: "38 · " + qsTr("Pantorrillas"), value: 38 },
        { text: "39 · " + qsTr("Escudo"), value: 39 },
        { text: "42 · " + qsTr("Diadema / Corona"), value: 42 },
        { text: "46 · " + qsTr("Capa (convención)"), value: 46 },
        { text: "52 · " + qsTr("Ropa interior"), value: 52 }
    ]

    readonly property var typeOptions: [
        { text: qsTr("Todos"), value: "" },
        { text: qsTr("Ropa"), value: "Clothing" },
        { text: qsTr("Ligera"), value: "LightArmor" },
        { text: qsTr("Pesada"), value: "HeavyArmor" }
    ]

    function getSlotIcon(slots, armorType) {
        if (!slots) return "📦"
        const s = String(slots)
        if (s.indexOf("30") !== -1 || s.indexOf("42") !== -1) return "🪖"
        if (s.indexOf("31") !== -1) return "💇"
        if (s.indexOf("32") !== -1) return "🥋"
        if (s.indexOf("33") !== -1 || s.indexOf("34") !== -1) return "🧤"
        if (s.indexOf("37") !== -1 || s.indexOf("38") !== -1) return "👢"
        if (s.indexOf("39") !== -1) return "🛡️"
        if (s.indexOf("35") !== -1) return "📿"
        if (s.indexOf("36") !== -1) return "💍"
        if (s.indexOf("46") !== -1) return "🧣"
        if (armorType === "Clothing") return "👕"
        return "🛡️"
    }

    function search() {
        studio.searchArmors(searchField.text, slotBox.currentValue, pluginField.text, root.activeTypeFilter, smpCheck.checked)
    }

    function resetFilters() {
        searchField.text = ""
        pluginField.text = ""
        root.activeTypeFilter = ""
        slotBox.currentIndex = 0
        smpCheck.checked = false
        search()
    }

    Timer { id: debounce; interval: 220; onTriggered: root.search() }

    // Menú contextual para prendas
    Menu {
        id: itemContextMenu
        property var itemData: null

        MenuItem {
            text: qsTr("👁 Ver en 3D")
            onTriggered: if (itemContextMenu.itemData) root.previewRequested(itemContextMenu.itemData.formKey)
        }
        MenuItem {
            text: qsTr("➕ Equipar en outfit activo")
            onTriggered: if (itemContextMenu.itemData) root.studio.outfit.addPiece(itemContextMenu.itemData, true)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("📋 Copiar FormKey")
            onTriggered: if (itemContextMenu.itemData?.formKey) root.studio.copyToClipboard(itemContextMenu.itemData.formKey)
        }
        MenuItem {
            text: qsTr("📋 Copiar EditorID")
            onTriggered: if (itemContextMenu.itemData?.editorId) root.studio.copyToClipboard(itemContextMenu.itemData.editorId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // ------------------------------------------------------------- BARRA SUPERIOR Y BÚSQUEDA
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5

            // Fila 1: Búsqueda y alternador Cuadrícula / Lista
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("🔍 Buscar prenda por nombre, EditorID...")
                    font.pixelSize: Theme.normal
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.panel
                        radius: Theme.radius
                        border.color: searchField.activeFocus ? Theme.borderFocus : Theme.border
                        border.width: 1
                    }
                    enabled: root.studio.phase === StudioController.Ready
                    onTextChanged: debounce.restart()

                    // Botón limpiar búsqueda
                    ToolButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 4
                        visible: searchField.text.length > 0
                        text: "✕"
                        implicitWidth: 20
                        implicitHeight: 20
                        font.pixelSize: 10
                        onClicked: { searchField.text = ""; root.search() }
                    }
                }

                // Selector Vista Lista vs Cuadrícula
                RowLayout {
                    spacing: 2
                    ToolButton {
                        text: "☰"
                        checkable: true
                        checked: !root.isGrid
                        onClicked: root.isGrid = false
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Vista en lista detallada")
                    }
                    ToolButton {
                        text: "⊞"
                        checkable: true
                        checked: root.isGrid
                        onClicked: root.isGrid = true
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Vista en cuadrícula de tarjetas")
                    }
                }
            }

            // Fila 2: Chips de tipo rápido y SMP
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                // Chips para filtrar por tipo de armadura
                Repeater {
                    model: root.typeOptions
                    delegate: Rectangle {
                        required property var modelData
                        implicitHeight: 24
                        implicitWidth: chipText.implicitWidth + 14
                        radius: Theme.radius
                        color: root.activeTypeFilter === modelData.value ? Theme.accent : Theme.panel
                        border.color: root.activeTypeFilter === modelData.value ? Theme.accentBorder : Theme.border
                        border.width: 1

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: modelData.text
                            font.pixelSize: Theme.small
                            color: root.activeTypeFilter === modelData.value ? "#ffffff" : Theme.textDim
                            font.bold: root.activeTypeFilter === modelData.value
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.activeTypeFilter = modelData.value
                                root.search()
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Badge botón de Físicas SMP
                Rectangle {
                    implicitHeight: 24
                    implicitWidth: smpRow.implicitWidth + 12
                    radius: Theme.radius
                    color: smpCheck.checked ? Theme.smpBadgeBg : Theme.panel
                    border.color: smpCheck.checked ? Theme.smpBadgeBorder : Theme.border
                    border.width: 1

                    RowLayout {
                        id: smpRow
                        anchors.centerIn: parent
                        spacing: 4
                        CheckBox {
                            id: smpCheck
                            visible: false
                            checked: false
                            onToggled: root.search()
                        }
                        Text {
                            text: "✦ SMP"
                            font.pixelSize: Theme.small
                            font.bold: true
                            color: smpCheck.checked ? Theme.smpBadgeText : Theme.textMuted
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            smpCheck.checked = !smpCheck.checked
                            root.search()
                        }
                    }

                    ToolTip.visible: smpArea.containsMouse
                    ToolTip.text: qsTr("Mostrar únicamente prendas con físicas HDT-SMP / FSMP")
                    MouseArea { id: smpArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }

                // Botón para expandir/colapsar filtros avanzados
                ToolButton {
                    text: root.showAdvancedFilters ? "⚙ ▴" : "⚙ ▾"
                    implicitHeight: 24
                    onClicked: root.showAdvancedFilters = !root.showAdvancedFilters
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Filtros avanzados (biped slot, mod origen)")
                }
            }

            // Fila 3: Filtros avanzados colapsables
            ColumnLayout {
                visible: root.showAdvancedFilters
                Layout.fillWidth: true
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: advCol.implicitHeight + 10
                    radius: Theme.radius
                    color: Theme.panelAlt
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: advCol
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Slot:")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                            }

                            ComboBox {
                                id: slotBox
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: root.slotOptions
                                onActivated: root.search()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Mod:")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                            }

                            TextField {
                                id: pluginField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Filtrar por plugin/mod (p. ej. Crimson.esp)")
                                font.pixelSize: Theme.small
                                onTextChanged: debounce.restart()
                            }

                            Button {
                                text: qsTr("Limpiar")
                                implicitHeight: 26
                                onClicked: root.resetFilters()
                            }
                        }
                    }
                }
            }

            // Fila 4: Contador y tip
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("<b>%1</b> prendas").arg(root.studio.armorTotal)
                    color: Theme.accent
                    font.pixelSize: Theme.small
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("Clic: 3D · Doble clic: equipar")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                }
            }
        }

        // ------------------------------------------------------------- LISTA DE PRENDAS (LIST VIEW)
        ListView {
            id: armorListView
            visible: !root.isGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 3
            model: root.studio.armorResults
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: card
                required property int index
                required property string formKey
                required property string formId
                required property string name
                required property string editorId
                required property string winningPlugin
                required property string winningMod
                required property string slots
                required property string armorType
                required property bool nonPlayable
                // Roles of StudioController.armorResults (a delegate with required properties has no
                // implicit `model`, so they are declared like the others).
                required property bool nifFound
                required property bool hasSmp

                width: armorListView.width - 6
                x: 3
                padding: 6
                hoverEnabled: true

                background: Rectangle {
                    radius: Theme.radius
                    color: card.hovered ? Theme.panelAlt : Theme.panel
                    border.color: card.hovered ? Theme.borderFocus : Theme.border
                    border.width: 1
                }

                onClicked: root.previewRequested(card.formKey)
                onDoubleClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(card.index), true)

                // Clic derecho para menú contextual
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        itemContextMenu.itemData = root.studio.armorResults.get(card.index)
                        itemContextMenu.popup()
                    }
                }

                contentItem: RowLayout {
                    spacing: 8

                    // Icono de categoría silhouette
                    Rectangle {
                        implicitWidth: 34
                        implicitHeight: 34
                        radius: Theme.radius
                        color: Theme.surface
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: root.getSlotIcon(card.slots, card.armorType)
                            font.pixelSize: 16
                        }
                    }

                    // Información principal
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        // Nombre y FormID
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: card.name && card.name.length > 0 ? card.name : card.editorId
                                color: Theme.text
                                font.pixelSize: Theme.normal
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Badge SMP
                            Rectangle {
                                visible: card.hasSmp
                                implicitWidth: smpText.implicitWidth + 8
                                implicitHeight: 16
                                radius: Theme.radiusSmall
                                color: Theme.smpBadgeBg
                                border.color: Theme.smpBadgeBorder
                                border.width: 1
                                Text {
                                    id: smpText
                                    anchors.centerIn: parent
                                    text: "✦ SMP"
                                    color: Theme.smpBadgeText
                                    font.pixelSize: Theme.micro
                                    font.bold: true
                                }
                            }

                            // FormID
                            Text {
                                text: card.formId && card.formId.length > 0 ? card.formId : (card.formKey ? card.formKey.split(":")[0] : "")
                                color: Theme.textMuted
                                font.pixelSize: Theme.micro
                                font.family: Theme.mono
                            }
                        }

                        // Tags de Slot, Tipo y Mod de procedencia
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            // Tag Slot
                            Rectangle {
                                implicitWidth: slotTag.implicitWidth + 6
                                implicitHeight: 16
                                radius: Theme.radiusSmall
                                color: Theme.surface
                                border.color: Theme.borderSubtle
                                Text {
                                    id: slotTag
                                    anchors.centerIn: parent
                                    text: "S: " + card.slots
                                    color: Theme.accent
                                    font.pixelSize: Theme.micro
                                }
                            }

                            // Tag Tipo
                            Rectangle {
                                visible: card.armorType && card.armorType.length > 0
                                implicitWidth: typeTag.implicitWidth + 6
                                implicitHeight: 16
                                radius: Theme.radiusSmall
                                color: Theme.surface
                                border.color: Theme.borderSubtle
                                Text {
                                    id: typeTag
                                    anchors.centerIn: parent
                                    text: card.armorType === "Clothing" ? qsTr("Ropa")
                                          : card.armorType === "LightArmor" ? qsTr("Ligera")
                                          : card.armorType === "HeavyArmor" ? qsTr("Pesada") : card.armorType
                                    color: Theme.textDim
                                    font.pixelSize: Theme.micro
                                }
                            }

                            // Nombre del mod
                            Text {
                                text: (card.winningMod && card.winningMod.length > 0 ? card.winningMod : card.winningPlugin)
                                color: Theme.textDim
                                font.pixelSize: Theme.micro
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            // Estado NIF
                            Rectangle {
                                implicitWidth: 8
                                implicitHeight: 8
                                radius: 4
                                color: card.nifFound ? Theme.ok : Theme.warn
                                ToolTip.visible: nifDotMouse.containsMouse
                                ToolTip.text: card.nifFound ? qsTr("Malla NIF disponible en VFS") : qsTr("Malla NIF ausente")
                                MouseArea { id: nifDotMouse; anchors.fill: parent; hoverEnabled: true }
                            }
                        }
                    }

                    // Botones de acción rápida
                    RowLayout {
                        spacing: 4

                        Button {
                            text: "👁"
                            implicitHeight: 24
                            implicitWidth: 26
                            font.pixelSize: 11
                            onClicked: root.previewRequested(card.formKey)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Ver en el visor 3D")
                        }

                        Button {
                            text: "+"
                            highlighted: true
                            implicitHeight: 24
                            implicitWidth: 26
                            font.pixelSize: 13
                            font.bold: true
                            onClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(card.index), true)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Equipar en el outfit")
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------- CUADRÍCULA DE TARJETAS (GRID VIEW)
        GridView {
            id: armorGridView
            visible: root.isGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(armorGridView.width / Math.max(1, Math.floor(armorGridView.width / 160)))
            cellHeight: 140
            model: root.studio.armorResults
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: gridCard
                required property int index
                required property string formKey
                required property string formId
                required property string name
                required property string editorId
                required property string winningPlugin
                required property string winningMod
                required property string slots
                required property string armorType
                required property bool nonPlayable
                required property bool nifFound
                required property bool hasSmp

                width: armorGridView.cellWidth - 6
                height: armorGridView.cellHeight - 6
                x: 3
                y: 3
                padding: 6
                hoverEnabled: true

                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: gridCard.hovered ? Theme.panelAlt : Theme.panel
                    border.color: gridCard.hovered ? Theme.borderFocus : Theme.border
                    border.width: 1
                }

                onClicked: root.previewRequested(gridCard.formKey)
                onDoubleClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(gridCard.index), true)

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        itemContextMenu.itemData = root.studio.armorResults.get(gridCard.index)
                        itemContextMenu.popup()
                    }
                }

                contentItem: ColumnLayout {
                    spacing: 4

                    // Icono silhouette central y badge SMP
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 40

                        Rectangle {
                            anchors.centerIn: parent
                            width: 36
                            height: 36
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.borderSubtle
                            Text {
                                anchors.centerIn: parent
                                text: root.getSlotIcon(gridCard.slots, gridCard.armorType)
                                font.pixelSize: 18
                            }
                        }

                        // Badge SMP esquina superior derecha
                        Rectangle {
                            visible: gridCard.hasSmp
                            anchors.right: parent.right
                            anchors.top: parent.top
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 3
                            color: Theme.smpBadgeBg
                            border.color: Theme.smpBadgeBorder
                            Text {
                                anchors.centerIn: parent
                                text: "✦"
                                color: Theme.smpBadgeText
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }

                        // Indicador NIF esquina superior izquierda
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            width: 6
                            height: 6
                            radius: 3
                            color: gridCard.nifFound ? Theme.ok : Theme.warn
                        }
                    }

                    // Título de la prenda
                    Text {
                        text: gridCard.name && gridCard.name.length > 0 ? gridCard.name : gridCard.editorId
                        color: Theme.text
                        font.pixelSize: Theme.small
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Mod origen
                    Text {
                        text: (gridCard.winningMod && gridCard.winningMod.length > 0 ? gridCard.winningMod : gridCard.winningPlugin)
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    // Botones inferiores de acción
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Button {
                            text: qsTr("👁 3D")
                            Layout.fillWidth: true
                            implicitHeight: 22
                            font.pixelSize: 10
                            onClicked: root.previewRequested(gridCard.formKey)
                        }

                        Button {
                            text: "+"
                            highlighted: true
                            implicitWidth: 26
                            implicitHeight: 22
                            font.pixelSize: 12
                            font.bold: true
                            onClicked: root.studio.outfit.addPiece(root.studio.armorResults.get(gridCard.index), true)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Equipar en outfit")
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------- ESTADO VACÍO (EMPTY STATE)
        Item {
            visible: root.studio.armorResults.count === 0 && root.studio.phase === StudioController.Ready
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(280, parent.width - 20)
                height: 180
                color: Theme.surface
                radius: Theme.radiusLarge
                border.color: Theme.borderSubtle
                border.width: 1

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    width: parent.width - 24

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "🧥"
                        font.pixelSize: 32
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Sin prendas encontradas")
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Prueba a ajustar la búsqueda o seleccionar otro tipo de filtro.")
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Restablecer filtros")
                        implicitHeight: 26
                        onClicked: root.resetFilters()
                    }
                }
            }
        }
    }
}
