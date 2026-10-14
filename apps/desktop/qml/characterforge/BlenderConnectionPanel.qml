import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    Text {
        text: qsTr("Blender")
        color: Theme.text
        font.bold: true
        font.pixelSize: Theme.title
    }
    Text {
        Layout.fillWidth: true
        text: root.forge.workspace.blender ?
                  (root.forge.workspace.blender.status === "verified" ?
                       qsTr("Última conversión con Blender %1").arg(root.forge.workspace.blender.version || "") :
                       qsTr("Ejecutable configurado · versión no comprobada")) : qsTr("No configurado")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Button {
            text: qsTr("Detectar en PATH")
            enabled: root.forge.hasProject && !root.forge.busy
            onClicked: root.forge.detectBlender()
        }
        Button {
            text: qsTr("Seleccionar…")
            enabled: root.forge.hasProject && !root.forge.busy
            onClicked: executablePicker.open()
        }
    }
    FileDialog {
        id: executablePicker
        title: qsTr("Selecciona el ejecutable de Blender")
        nameFilters: ["Ejecutable de Blender (blender.exe blender)", "Todos los archivos (*)"]
        onAccepted: root.forge.selectBlenderExecutable(selectedFile.toString())
    }
    Text {
        Layout.fillWidth: true
        text: root.forge.workspace.blender ? root.forge.workspace.blender.executable : ""
        color: Theme.textMuted
        elide: Text.ElideMiddle
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Convierte FBX, OBJ o BLEND a GLB validado. El resultado se guarda en la biblioteca del proyecto; aún no se convierte a NIF de Skyrim.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    BlenderOperations {
        Layout.fillWidth: true
        forge: root.forge
    }
    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: Theme.border
    }
    Text {
        text: qsTr("Blender MCP · independiente del bridge")
        color: Theme.text
        font.bold: true
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("No configurado. El adaptador MCP no está implementado; no se realizan llamadas ni se almacenan credenciales.")
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Button {
        text: qsTr("Conectar MCP · adaptador pendiente")
        enabled: false
    }
}
