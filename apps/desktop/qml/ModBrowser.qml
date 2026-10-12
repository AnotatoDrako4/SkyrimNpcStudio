import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Mod explorer: MO2 priority order, contributed files, plugins, NPC/armor counts, conflicts, dependencies.
ColumnLayout {
    id: root
    required property StudioController studio
    property var details: ({})
    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        TextField {
            id: filter
            Layout.fillWidth: true
            placeholderText: qsTr("Filtrar por mod o plugin")
            onTextChanged: root.studio.filterMods(text, enabledOnly.checked)
        }
        CheckBox {
            id: enabledOnly
            text: qsTr("Activos")
            onToggled: root.studio.filterMods(filter.text, checked)
        }
    }

    SplitView {
        orientation: Qt.Vertical
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: list
            SplitView.fillHeight: true
            clip: true
            model: root.studio.mods
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property string name
                required property bool isEnabled
                required property int priority
                required property string kind
                required property var fileCount
                required property string plugins
                width: ListView.view.width
                height: kind === "separator" ? 24 : implicitHeight
                onClicked: root.details = root.studio.modDetails(name)
                background: Rectangle {
                    color: parent.highlighted ? Theme.accentDim : parent.hovered ? Theme.panelAlt : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; opacity: 0.5 }
                }
                contentItem: RowLayout {
                    spacing: 6
                    Text {
                        text: row.kind === "separator" ? "" : row.priority
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        font.family: Theme.mono
                        Layout.preferredWidth: 32
                    }
                    Text {
                        text: row.kind === "separator" ? "— " + row.name.replace("_separator", "") + " —" : row.name
                        color: row.kind === "separator" ? Theme.accent : row.isEnabled ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.small
                        font.bold: row.kind === "separator"
                        font.strikeout: row.kind === "mod" && !row.isEnabled
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: row.kind === "mod" && row.isEnabled
                        text: row.fileCount + qsTr(" arch.")
                        color: Theme.textDim
                        font.pixelSize: Theme.small - 1
                    }
                    StatusBadge { visible: row.kind === "foreign"; text: qsTr("externo"); tone: Theme.textDim }
                }
            }
        }

        ScrollView {
            SplitView.preferredHeight: 300
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 4
                SectionHeader { title: root.details.name ?? qsTr("Selecciona un mod"); detail: root.details.active ? qsTr("activo") : "" }
                Text {
                    visible: root.details.note !== undefined
                    text: root.details.note ?? ""
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.small
                    Layout.fillWidth: true
                }
                KeyValue { visible: root.details.active === true; label: qsTr("Archivos"); value: (root.details.files ?? 0) + "" }
                KeyValue { visible: root.details.active === true; label: qsTr("Sobrescribe"); value: (root.details.winsTotal ?? 0) + qsTr(" archivos de otros mods") }
                KeyValue { visible: root.details.active === true; label: qsTr("Sobrescrito"); value: (root.details.losesTotal ?? 0) + qsTr(" archivos") ; valueColor: (root.details.losesTotal ?? 0) > 0 ? Theme.warn : Theme.text }
                Repeater {
                    model: root.details.plugins ?? []
                    delegate: KeyValue {
                        required property var modelData
                        label: modelData.active ? qsTr("Plugin activo") : qsTr("Plugin inactivo")
                        value: modelData.name + (modelData.light ? " [ESL]" : "") + " — " + modelData.npcs + " NPC, "
                               + modelData.armors + " ARMO, " + modelData.outfits + " OTFT"
                    }
                }
                Repeater {
                    model: root.details.dependencies ?? []
                    delegate: KeyValue { required property string modelData; label: qsTr("Requiere"); value: modelData }
                }
                Text { visible: (root.details.loses ?? []).length > 0; text: qsTr("Archivos perdidos (muestra) → mod ganador"); color: Theme.warn; font.pixelSize: Theme.small }
                Repeater {
                    model: root.details.loses ?? []
                    delegate: Text { required property string modelData; text: modelData; color: Theme.textDim; font.pixelSize: Theme.small - 1; font.family: Theme.mono; elide: Text.ElideMiddle; Layout.fillWidth: true }
                }
            }
        }
    }
}
