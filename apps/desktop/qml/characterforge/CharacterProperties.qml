import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

ForgeEditorSection {
    id: root
    Layout.fillWidth: true
    Text {
        text: qsTr("Nombre del personaje")
        color: Theme.textDim
    }
    TextField {
        Layout.fillWidth: true
        text: root.forge.characterName
        onEditingFinished: root.forge.setCharacterName(text)
    }
    Text {
        text: "EditorID"
        color: Theme.textDim
    }
    TextField {
        Layout.fillWidth: true
        text: root.forge.editorId
        placeholderText: "SNS_MiPersonaje"
        onEditingFinished: root.forge.setEditorId(text)
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "race"
        title: qsTr("Raza")
        value: root.forge.npc.raceFormKey || ""
        onSelected: record => root.set("raceFormKey", record.formKey)
    }
    Text {
        text: qsTr("Sexo")
        color: Theme.textDim
    }
    ComboBox {
        Layout.fillWidth: true
        model: [qsTr("Masculino"), qsTr("Femenino")]
        currentIndex: root.forge.npc.female ? 1 : 0
        onActivated: root.set("female", currentIndex === 1)
    }
    ForgeRecordSelector {
        Layout.fillWidth: true
        forge: root.forge
        kind: "voice"
        title: qsTr("Voz")
        value: root.forge.npc.voiceFormKey || ""
        onSelected: record => root.set("voiceFormKey", record.formKey)
    }
    Text {
        text: qsTr("Altura (0.5–2)")
        color: Theme.textDim
    }
    TextField {
        Layout.fillWidth: true
        text: String(root.forge.npc.height || 1)
        validator: DoubleValidator {
            bottom: 0.5
            top: 2
            locale: "C"
        }
        onEditingFinished: root.set("height", Number(text))
    }
    Text {
        text: qsTr("Peso (0–100)")
        color: Theme.textDim
    }
    SpinBox {
        from: 0
        to: 100
        value: root.forge.npc.weight === undefined ? 50 : root.forge.npc.weight
        onValueModified: {
            root.set("weight", value);
            if (root.forge.npc.face) {
                let f = JSON.parse(JSON.stringify(root.forge.npc.face));
                f.weight = value;
                root.set("face", f);
            }
        }
    }
}
