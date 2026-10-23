import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Catálogo visual y buscador de ropa y armaduras (Creative Workspace). The search stays on top; mod, type,
// SMP and slot filters say how many are active and clear in one click. Rows and cards keep the full name,
// EditorID, FormKey, slots and plugin in their tooltip; Space previews in 3D, Enter equips, Ctrl+C copies
// the FormKey. The lists stay virtualized (ListView / GridView over StudioController.armorResults).
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

    // The first biped slot of a piece, shown on its tile; the tooltip names every slot it uses.
    function primarySlot(slots) {
        const first = String(slots || "").split(/[^0-9]+/).filter(t => t.length > 0)[0]
        return first || "—"
    }
    function slotNames(slots) {
        const numbers = String(slots || "").split(/[^0-9]+/).filter(t => t.length > 0)
        return numbers.map(n => {
            const known = root.slotOptions.find(o => String(o.value) === n)
            return known ? known.text : n
        }).join(", ")
    }
    function typeText(armorType) {
        return armorType === "Clothing" ? qsTr("Ropa") : armorType === "LightArmor" ? qsTr("Ligera")
             : armorType === "HeavyArmor" ? qsTr("Pesada") : armorType
    }
    function detailsOf(piece) {
        return (piece.name && piece.name.length > 0 ? piece.name : qsTr("(sin nombre)")) + "\n"
             + piece.editorId + " · " + piece.formKey + "\n"
             + qsTr("Slots: %1").arg(root.slotNames(piece.slots)) + (piece.armorType ? " · " + root.typeText(piece.armorType) : "") + "\n"
             + qsTr("Gana: %1").arg(piece.winningPlugin) + (piece.winningMod ? " [" + piece.winningMod + "]" : "") + "\n"
             + (piece.nifFound ? qsTr("Malla NIF disponible en el VFS") : qsTr("Malla NIF ausente en el VFS"))
             + (piece.hasSmp ? " · " + qsTr("físicas SMP") : "")
    }
    function equip(index) { root.studio.outfit.addPiece(root.studio.armorResults.get(index), true) }
    // Used by the empty viewer ("Ver prendas") and the command palette.
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }

    // Mod filter: every plugin that defines or overrides armors, labelled with its MO2 mod.
    readonly property var modOptions: [{ text: qsTr("Todos los mods"), value: "" }].concat(
        (root.studio.armorPlugins || []).map(function (p) {
            return { text: (p.mod && p.mod.length > 0 ? p.mod + " — " : "") + p.plugin + " (" + p.armors + ")", value: p.plugin }
        }))

    readonly property int activeFilters: (modBox.currentIndex > 0 ? 1 : 0) + (root.activeTypeFilter !== "" ? 1 : 0)
                                         + (slotBox.currentIndex > 0 ? 1 : 0) + (smpCheck.checked ? 1 : 0)

    function search() {
        studio.searchArmors(searchField.text, slotBox.currentValue, modBox.currentValue || "", root.activeTypeFilter, smpCheck.checked)
    }

    function resetFilters() {
        searchField.text = ""
        modBox.currentIndex = 0
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
            text: qsTr("Ver en 3D")
            onTriggered: if (itemContextMenu.itemData) root.previewRequested(itemContextMenu.itemData.formKey)
        }
        MenuItem {
            text: qsTr("Equipar en el outfit activo")
            onTriggered: if (itemContextMenu.itemData) root.studio.outfit.addPiece(itemContextMenu.itemData, true)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Copiar FormKey")
            onTriggered: if (itemContextMenu.itemData?.formKey) root.studio.copyToClipboard(itemContextMenu.itemData.formKey)
        }
        MenuItem {
            text: qsTr("Copiar EditorID")
            onTriggered: if (itemContextMenu.itemData?.editorId) root.studio.copyToClipboard(itemContextMenu.itemData.editorId)
        }
    }

    // Keyboard on a list or grid: Space previews, Enter equips, Ctrl+C copies the FormKey, the menu key opens the menu.
    function handleKeys(view, event) {
        const item = view.currentItem
        if (!item) return
        if (event.key === Qt.Key_Space) { root.previewRequested(item.formKey); event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { root.equip(item.index); event.accepted = true }
        else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) { root.studio.copyToClipboard(item.formKey); event.accepted = true }
        else if (event.key === Qt.Key_Menu) {
            itemContextMenu.itemData = root.studio.armorResults.get(item.index)
            itemContextMenu.popup(item, item.width / 2, item.height / 2)
            event.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        // ------------------------------------------------------------- BÚSQUEDA Y FILTROS
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            TextField {
                id: searchField
                objectName: "armorSearchField"
                Layout.fillWidth: true
                leftPadding: Theme.sp(30)
                rightPadding: clearArmorSearch.visible ? clearArmorSearch.width + Theme.sp(4) : Theme.sp(8)
                placeholderText: qsTr("Buscar prenda: nombre o EditorID")
                Accessible.name: qsTr("Buscar prenda")
                ToolTip.visible: hovered && text.length === 0
                ToolTip.delay: 800
                ToolTip.text: qsTr("Nombre o EditorID. Flecha abajo: ir a los resultados.")
                enabled: root.studio.phase === StudioController.Ready
                onTextChanged: debounce.restart()
                Keys.onDownPressed: (root.isGrid ? armorGridView : armorListView).forceActiveFocus()
                Icon {
                    name: "search"
                    size: Theme.small + 1
                    color: Theme.textMuted
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.sp(10)
                    anchors.verticalCenter: parent.verticalCenter
                }
                IconToolButton {
                    id: clearArmorSearch
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: searchField.height - 4
                    glyph: "close"
                    label: qsTr("Borrar la búsqueda")
                    showLabel: false
                    onClicked: { searchField.text = ""; root.search() }
                }
            }

            // Mod de origen (ESP/ESM/ESL y su mod de MO2)
            ComboBox {
                id: modBox
                objectName: "armorModFilter"
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: root.modOptions
                Accessible.name: qsTr("Filtrar por mod")
                enabled: root.studio.phase === StudioController.Ready
                onActivated: root.search()
                ToolTip.visible: hovered && currentIndex > 0
                ToolTip.text: currentText
            }

            // Tipo de armadura
            SegmentedControl {
                objectName: "armorTypeFilter"
                Layout.fillWidth: true
                stretch: true
                model: root.typeOptions.map(o => o.text)
                currentIndex: Math.max(0, root.typeOptions.findIndex(o => o.value === root.activeTypeFilter))
                onActivated: index => { root.activeTypeFilter = root.typeOptions[index].value; root.search() }
            }

            // SMP, slot, vista y limpiar
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                CheckBox { id: smpCheck; visible: false; checked: false }
                IconToolButton {
                    objectName: "armorSmpFilter"
                    label: qsTr("Solo SMP")
                    checkable: true
                    checked: smpCheck.checked
                    tone: smpCheck.checked ? Theme.smpBadgeText : Theme.text
                    tip: qsTr("Mostrar únicamente prendas con físicas HDT-SMP / FSMP")
                    onClicked: { smpCheck.checked = !smpCheck.checked; root.search() }
                }
                IconToolButton {
                    objectName: "armorSlotFilterButton"
                    glyph: "filter"
                    label: slotBox.currentIndex > 0 ? qsTr("Slot %1").arg(slotBox.currentValue) : qsTr("Slot")
                    checkable: true
                    checked: root.showAdvancedFilters
                    tone: slotBox.currentIndex > 0 ? Theme.accent : Theme.text
                    tip: qsTr("Filtrar por biped slot")
                    onClicked: root.showAdvancedFilters = !root.showAdvancedFilters
                }
                IconToolButton {
                    objectName: "armorClearFiltersButton"
                    visible: root.activeFilters > 0
                    glyph: "clear"
                    label: qsTr("Limpiar filtros (%1)").arg(root.activeFilters)
                    showLabel: false
                    onClicked: root.resetFilters()
                }
                Item { Layout.fillWidth: true }
                SegmentedControl {
                    objectName: "armorViewSwitch"
                    compact: true
                    model: [{ label: qsTr("Lista"), glyph: "list" }, { label: qsTr("Cuadrícula"), glyph: "grid" }]
                    currentIndex: root.isGrid ? 1 : 0
                    onActivated: index => root.isGrid = index === 1
                }
            }

            // Slot (plegable)
            RowLayout {
                visible: root.showAdvancedFilters
                Layout.fillWidth: true
                spacing: Theme.spacingSm
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
                    Accessible.name: qsTr("Filtrar por biped slot")
                    onActivated: root.search()
                }
            }

            // Contador y ayuda
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "armorResultCount"
                    text: root.studio.phase !== StudioController.Ready ? qsTr("El catálogo aparece cuando termina de indexarse el perfil.")
                        : qsTr("Mostrando %1 de %2 prendas").arg(root.isGrid ? armorGridView.count : armorListView.count).arg(root.studio.armorTotal)
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Clic: 3D · Doble clic: equipar")
                    color: Theme.textMuted
                    font.pixelSize: Theme.small
                    visible: root.width >= Theme.px(300)
                }
            }
        }

        // ------------------------------------------------------------- LISTA DE PRENDAS (LIST VIEW)
        ListView {
            id: armorListView
            objectName: "armorListView"
            visible: !root.isGrid && count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.sp(3)
            model: root.studio.armorResults
            currentIndex: -1
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Keys.onPressed: event => root.handleKeys(armorListView, event)
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

                width: armorListView.width - Theme.sp(4)
                x: Theme.sp(2)
                padding: Theme.sp(6)
                leftPadding: Theme.sp(8)
                hoverEnabled: true
                Accessible.name: (name && name.length > 0 ? name : editorId) + ", " + formKey
                ToolTip.visible: hovered
                ToolTip.delay: 900
                ToolTip.text: root.detailsOf(card)

                background: Rectangle {
                    radius: Theme.radius
                    color: card.ListView.isCurrentItem ? Theme.accentSubtle : card.hovered ? Theme.panelAlt : Theme.panel
                    border.color: card.ListView.isCurrentItem && armorListView.activeFocus ? Theme.borderFocus
                                : card.hovered ? Theme.accentBorder : Theme.border
                    border.width: card.ListView.isCurrentItem && armorListView.activeFocus ? Theme.focusWidth : 1
                }

                onClicked: {
                    armorListView.currentIndex = card.index
                    armorListView.forceActiveFocus()
                    root.previewRequested(card.formKey)
                }
                onDoubleClicked: root.equip(card.index)

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
                    spacing: Theme.sp(8)

                    // Primer slot del objeto
                    Rectangle {
                        implicitWidth: Theme.controlHeight + Theme.sp(4)
                        implicitHeight: implicitWidth
                        radius: Theme.radius
                        color: Theme.surface
                        border.color: Theme.borderSubtle
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: root.primarySlot(card.slots)
                            color: Theme.accent
                            font.family: Theme.mono
                            font.pixelSize: Theme.normal
                            font.bold: true
                        }
                    }

                    // Información principal
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(2)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp(6)

                            Text {
                                text: card.name && card.name.length > 0 ? card.name : card.editorId
                                color: Theme.text
                                font.pixelSize: Theme.normal
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Badge SMP
                            StatusBadge {
                                visible: card.hasSmp
                                text: "SMP"
                                tone: Theme.smpBadgeText
                            }
                        }

                        // Slots, tipo y FormID en la segunda línea; el mod con todo el ancho en la tercera
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp(6)
                            Text {
                                text: qsTr("S %1").arg(card.slots)
                                color: Theme.accent
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                                Layout.maximumWidth: implicitWidth
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: card.armorType && card.armorType.length > 0
                                text: root.typeText(card.armorType)
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: (card.formId && card.formId.length > 0 ? card.formId : (card.formKey ? card.formKey.split(":")[0] : ""))
                                color: Theme.textMuted
                                font.pixelSize: Theme.small
                                font.family: Theme.mono
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp(6)
                            // Estado NIF (el tooltip de la fila lo dice en palabras)
                            Rectangle {
                                implicitWidth: 8
                                implicitHeight: 8
                                radius: 4
                                color: card.nifFound ? Theme.ok : Theme.warn
                                Accessible.name: card.nifFound ? qsTr("Malla NIF disponible") : qsTr("Malla NIF ausente")
                            }
                            Text {
                                text: (card.winningMod && card.winningMod.length > 0 ? card.winningMod : card.winningPlugin)
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Acciones rápidas
                    IconToolButton {
                        glyph: "eye"
                        label: qsTr("Ver en el visor 3D")
                        showLabel: false
                        shortcutText: qsTr("Espacio")
                        onClicked: root.previewRequested(card.formKey)
                    }
                    IconToolButton {
                        glyph: "add"
                        label: qsTr("Equipar en el outfit")
                        showLabel: false
                        emphasized: true
                        shortcutText: qsTr("Enter")
                        onClicked: root.equip(card.index)
                    }
                }
            }
        }

        // ------------------------------------------------------------- CUADRÍCULA DE TARJETAS (GRID VIEW)
        GridView {
            id: armorGridView
            objectName: "armorGridView"
            visible: root.isGrid && count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property int cardMin: Theme.px(170)
            cellWidth: Math.floor(armorGridView.width / Math.max(1, Math.floor(armorGridView.width / cardMin)))
            cellHeight: Theme.controlHeight * 2 + Theme.normal + Theme.small + Theme.sp(40)
            model: root.studio.armorResults
            currentIndex: -1
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Keys.onPressed: event => root.handleKeys(armorGridView, event)
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

                width: armorGridView.cellWidth - Theme.sp(6)
                height: armorGridView.cellHeight - Theme.sp(6)
                x: Theme.sp(3)
                y: Theme.sp(3)
                padding: Theme.sp(6)
                hoverEnabled: true
                Accessible.name: (name && name.length > 0 ? name : editorId) + ", " + formKey
                ToolTip.visible: hovered
                ToolTip.delay: 900
                ToolTip.text: root.detailsOf(gridCard)

                background: Rectangle {
                    radius: Theme.radiusLarge
                    color: gridCard.GridView.isCurrentItem ? Theme.accentSubtle : gridCard.hovered ? Theme.panelAlt : Theme.panel
                    border.color: gridCard.GridView.isCurrentItem && armorGridView.activeFocus ? Theme.borderFocus
                                : gridCard.hovered ? Theme.accentBorder : Theme.border
                    border.width: gridCard.GridView.isCurrentItem && armorGridView.activeFocus ? Theme.focusWidth : 1
                }

                onClicked: {
                    armorGridView.currentIndex = gridCard.index
                    armorGridView.forceActiveFocus()
                    root.previewRequested(gridCard.formKey)
                }
                onDoubleClicked: root.equip(gridCard.index)

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        itemContextMenu.itemData = root.studio.armorResults.get(gridCard.index)
                        itemContextMenu.popup()
                    }
                }

                contentItem: ColumnLayout {
                    spacing: Theme.sp(4)

                    // Slot, SMP y NIF
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(6)
                        Rectangle {
                            implicitWidth: Theme.controlHeight
                            implicitHeight: Theme.controlHeight
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.borderSubtle
                            Text {
                                anchors.centerIn: parent
                                text: root.primarySlot(gridCard.slots)
                                color: Theme.accent
                                font.family: Theme.mono
                                font.pixelSize: Theme.normal
                                font.bold: true
                            }
                        }
                        Item { Layout.fillWidth: true }
                        StatusBadge {
                            visible: gridCard.hasSmp
                            text: "SMP"
                            tone: Theme.smpBadgeText
                        }
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: gridCard.nifFound ? Theme.ok : Theme.warn
                        }
                    }

                    Text {
                        text: gridCard.name && gridCard.name.length > 0 ? gridCard.name : gridCard.editorId
                        color: Theme.text
                        font.pixelSize: Theme.normal
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: (gridCard.winningMod && gridCard.winningMod.length > 0 ? gridCard.winningMod : gridCard.winningPlugin)
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp(4)
                        IconToolButton {
                            Layout.fillWidth: true
                            glyph: "eye"
                            label: qsTr("Ver en 3D")
                            onClicked: root.previewRequested(gridCard.formKey)
                        }
                        IconToolButton {
                            glyph: "add"
                            label: qsTr("Equipar en el outfit")
                            showLabel: false
                            emphasized: true
                            onClicked: root.equip(gridCard.index)
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------- ESTADO VACÍO (EMPTY STATE)
        EmptyState {
            visible: root.studio.armorResults.count === 0 && root.studio.phase === StudioController.Ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: "search"
            title: qsTr("Sin prendas encontradas")
            message: root.activeFilters > 0 || searchField.text.length > 0
                     ? qsTr("Ninguna prenda coincide con la búsqueda y los %1 filtro(s) activos.").arg(root.activeFilters)
                     : qsTr("El perfil no tiene prendas indexadas.")
            actions: root.activeFilters > 0 || searchField.text.length > 0
                     ? [{ label: qsTr("Restablecer filtros"), trigger: () => root.resetFilters() }] : []
        }
    }
}
