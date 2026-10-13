import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ForgeEditorSection {
    id: root
    Layout.fillWidth: true
    Text {
        Layout.fillWidth: true
        text: qsTr("Sin ubicación no se genera ACHR. Solo se admite una celda interior seleccionada explícitamente; coordenadas en unidades del juego y giro en radianes.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "cell"
        title: qsTr("Celda interior (opcional)")
        value: root.forge.npc.placement ? root.forge.npc.placement.cellFormKey : ""
        onSelected: record => root.set("placement", {
                cellFormKey: record.formKey,
                x: 0,
                y: 0,
                z: 0,
                rotationZ: 0
            })
    }
    Button {
        text: qsTr("Sin ubicación")
        enabled: !!root.forge.npc.placement
        onClicked: root.set("placement", null)
    }
    Repeater {
        model: ["x", "y", "z", "rotationZ"]
        TextField {
            required property var modelData
            Layout.fillWidth: true
            enabled: !!root.forge.npc.placement
            placeholderText: modelData
            text: root.forge.npc.placement ? String(root.forge.npc.placement[modelData]) : ""
            validator: DoubleValidator {
                locale: "C"
            }
            onEditingFinished: {
                if (!root.forge.npc.placement)
                    return;
                let p = Object.assign({}, root.forge.npc.placement);
                p[modelData] = Number(text);
                root.set("placement", p);
            }
        }
    }
}
