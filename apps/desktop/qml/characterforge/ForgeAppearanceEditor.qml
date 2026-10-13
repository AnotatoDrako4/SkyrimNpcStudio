import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ForgeEditorSection {
    id: root
    Layout.fillWidth: true
    Text {
        Layout.fillWidth: true
        text: qsTr("Configuración del registro facial. FaceGen geométrico pendiente; no hay vista previa certificada.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "headPart"
        title: qsTr("Agregar cabeza, pelo, ojos o cejas")
        onSelected: record => root.append("headPartFormKeys", record.formKey)
    }
    Repeater {
        model: root.forge.npc.headPartFormKeys || []
        RowLayout {
            required property var modelData
            required property int index
            Text {
                Layout.fillWidth: true
                text: root.recordLabel(modelData)
                color: Theme.textDim
                elide: Text.ElideRight
            }
            Button {
                text: "×"
                onClicked: root.remove("headPartFormKeys", index)
            }
        }
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "hairColor"
        title: qsTr("Color de pelo")
        value: root.forge.npc.hairColorFormKey || ""
        onSelected: record => root.set("hairColorFormKey", record.formKey)
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "headTexture"
        title: qsTr("Textura de cabeza")
        value: root.forge.npc.headTextureFormKey || ""
        onSelected: record => root.set("headTextureFormKey", record.formKey)
    }
    Text {
        text: qsTr("Morphs del registro (-1–1)")
        color: Theme.textDim
    }
    Repeater {
        model: [qsTr("Nariz longitud"), qsTr("Nariz altura"), qsTr("Mandíbula altura"), qsTr("Mandíbula anchura"), qsTr("Mandíbula profundidad"), qsTr("Mejillas altura"), qsTr("Mejillas profundidad"), qsTr("Ojos altura"), qsTr("Ojos separación"), qsTr("Cejas altura"), qsTr("Cejas separación"), qsTr("Cejas profundidad"), qsTr("Labios altura"), qsTr("Labios profundidad"), qsTr("Mentón anchura"), qsTr("Mentón altura"), qsTr("Mentón mordida"), qsTr("Ojos profundidad")]
        ColumnLayout {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Text {
                text: root.recordLabel(modelData)
                color: Theme.textDim
            }
            Slider {
                Layout.fillWidth: true
                from: -1
                to: 1
                value: root.face().sliders[index]
                onPressedChanged: if (!pressed)
                    root.setMorph(index, value)
            }
        }
    }
    Text {
        text: qsTr("Presets de nariz, ojos y boca")
        color: Theme.textDim
    }
    Repeater {
        model: [0, 2, 3]
        SpinBox {
            required property var modelData
            from: -1
            to: 65535
            value: root.face().presets[modelData]
            onValueModified: {
                let f = JSON.parse(JSON.stringify(root.face()));
                f.presets[modelData] = value;
                root.set("face", f);
            }
        }
    }
    Text {
        text: qsTr("Tono de piel (RGB)")
        color: Theme.textDim
    }
    RowLayout {
        Repeater {
            model: ["r", "g", "b"]
            SpinBox {
                required property var modelData
                from: 0
                to: 255
                value: root.face().skinTone[modelData]
                onValueModified: {
                    let f = JSON.parse(JSON.stringify(root.face()));
                    f.skinTone[modelData] = value;
                    root.set("face", f);
                }
            }
        }
    }
    Text {
        text: qsTr("Tint mask del registro (índice / preset)")
        color: Theme.textDim
    }
    RowLayout {
        SpinBox {
            id: tintIndex
            from: 0
            to: 65535
            value: 24
        }
        SpinBox {
            id: tintPreset
            from: -32768
            to: 32767
        }
        Button {
            text: qsTr("Agregar")
            onClicked: {
                let f = JSON.parse(JSON.stringify(root.face()));
                f.tints.push({
                    index: tintIndex.value,
                    preset: tintPreset.value,
                    value: 1,
                    color: Object.assign({}, f.skinTone)
                });
                root.set("face", f);
            }
        }
    }
    Repeater {
        model: root.face().tints
        RowLayout {
            required property var modelData
            required property int index
            Text {
                text: qsTr("Máscara %1, preset %2").arg(modelData.index).arg(modelData.preset)
                color: Theme.textDim
            }
            Button {
                text: "×"
                onClicked: {
                    let f = JSON.parse(JSON.stringify(root.face()));
                    f.tints.splice(index, 1);
                    root.set("face", f);
                }
            }
        }
    }
}
