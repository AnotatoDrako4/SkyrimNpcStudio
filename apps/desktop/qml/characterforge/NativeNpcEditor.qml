import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    enabled: forge.hasProject && !forge.busy
    TabBar {
        id: tabs
        Layout.fillWidth: true
        TabButton {
            text: qsTr("Identidad")
        }
        TabButton {
            text: qsTr("Datos")
        }
        TabButton {
            text: qsTr("Rostro")
        }
        TabButton {
            text: qsTr("Equipo")
        }
        TabButton {
            text: qsTr("Lugar")
        }
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            CharacterProperties {
                Layout.fillWidth: true
                forge: root.forge
                visible: tabs.currentIndex === 0
            }
            StatsEditor {
                Layout.fillWidth: true
                forge: root.forge
                visible: tabs.currentIndex === 1
            }
            ForgeAppearanceEditor {
                Layout.fillWidth: true
                forge: root.forge
                visible: tabs.currentIndex === 2
            }
            CharacterInventory {
                Layout.fillWidth: true
                forge: root.forge
                visible: tabs.currentIndex === 3
            }
            ForgePlacementEditor {
                Layout.fillWidth: true
                forge: root.forge
                visible: tabs.currentIndex === 4
            }
        }
    }
}
