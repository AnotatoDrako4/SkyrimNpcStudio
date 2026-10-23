import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Command palette (Ctrl+Shift+P): type a few letters of a command, move with the arrows, Enter runs it, Esc
// closes. Every command calls the same handler as its menu entry or button (the window builds the list), so
// nothing here can do something the window cannot. When it closes, the keyboard focus goes back to the
// control that had it.
//   commands: [{ id, category, label, shortcut, enabled: bool, run: function }]
Popup {
    id: palette
    objectName: "commandPalette"
    property var commands: []
    property Item restoreFocus: null
    property int ranCount: 0              // how many commands ran through the palette (automation reads it)
    property string lastRun: ""

    parent: Overlay.overlay
    modal: true
    focus: true
    x: Math.round(((parent ? parent.width : 800) - width) / 2)
    y: Math.round((parent ? parent.height : 600) * 0.12)
    width: Math.min(Theme.px(620), (parent ? parent.width : 800) - Theme.sp(40))
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusLarge
        border.color: Theme.accentBorder
        border.width: 1
    }

    // Every word typed must appear in the label or the category (case and accents ignored).
    function normalize(text) { return String(text).toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "") }
    function matches(command, words) {
        const hay = normalize(command.category + " " + command.label + " " + (command.shortcut || ""))
        return words.every(w => hay.indexOf(w) >= 0)
    }
    readonly property var filtered: {
        const words = normalize(query.text).split(/\s+/).filter(w => w.length > 0)
        return commands.filter(c => matches(c, words))
    }

    function openPalette(focusItem) {
        restoreFocus = focusItem
        query.text = ""
        list.currentIndex = 0
        open()
    }
    function runAt(index) {
        const command = filtered[index]
        if (!command || command.enabled === false) return
        lastRun = command.id
        ranCount += 1
        close()
        Qt.callLater(command.run)   // after the popup is gone: dialogs open on top, focus goes where the command puts it
    }
    // By id, for automation and for keyboard-only use from other places.
    function runCommand(id) {
        const command = commands.find(c => c.id === id)
        if (!command || command.enabled === false) return false
        lastRun = id
        ranCount += 1
        command.run()
        return true
    }

    onOpened: query.forceActiveFocus()
    onClosed: if (restoreFocus && restoreFocus.visible) restoreFocus.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 0
        TextField {
            id: query
            objectName: "commandPaletteQuery"
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            leftPadding: Theme.sp(30)
            placeholderText: qsTr("Escribe un comando: «forge», «panel», «buscar», «escala»…")
            Accessible.name: qsTr("Buscar comando")
            onTextChanged: list.currentIndex = 0
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            Keys.onReturnPressed: palette.runAt(list.currentIndex)
            Keys.onEnterPressed: palette.runAt(list.currentIndex)
            Icon {
                name: "search"
                size: Theme.small + 1
                color: Theme.textMuted
                anchors.left: parent.left
                anchors.leftMargin: Theme.sp(10)
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
        ListView {
            id: list
            objectName: "commandPaletteList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.rowHeight * 10)
            clip: true
            model: palette.filtered
            currentIndex: 0
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: Theme.rowHeight
                enabled: modelData.enabled !== false
                highlighted: ListView.isCurrentItem
                Accessible.name: modelData.category + ": " + modelData.label
                onClicked: palette.runAt(index)
                contentItem: RowLayout {
                    spacing: Theme.spacingMd
                    Text {
                        text: row.modelData.category
                        color: Theme.textMuted
                        font.pixelSize: Theme.small
                        Layout.preferredWidth: Theme.px(110)
                        elide: Text.ElideRight
                    }
                    Text {
                        text: row.modelData.label
                        color: row.enabled ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.normal
                        font.bold: row.highlighted
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: (row.modelData.shortcut || "").length > 0
                        text: row.modelData.shortcut || ""
                        color: Theme.textDim
                        font.family: Theme.mono
                        font.pixelSize: Theme.small
                    }
                }
                background: Rectangle {
                    color: row.highlighted ? Theme.accentSubtle : row.hovered ? Theme.panelAlt : "transparent"
                    Rectangle {
                        visible: row.highlighted
                        width: 3
                        height: parent.height
                        color: Theme.accent
                    }
                }
            }
        }
        Text {
            visible: list.count === 0
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLg
            text: qsTr("Ningún comando coincide con «%1».").arg(query.text)
            color: Theme.textDim
            font.pixelSize: Theme.normal
            horizontalAlignment: Text.AlignHCenter
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
        Text {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSm
            text: qsTr("↑↓ moverse · Enter ejecutar · Esc cerrar · %1 comandos").arg(palette.commands.length)
            color: Theme.textMuted
            font.pixelSize: Theme.small
            horizontalAlignment: Text.AlignRight
        }
    }
}
