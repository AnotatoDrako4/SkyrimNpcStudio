import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// One setting of the Settings Center, drawn from its descriptor (AppSettings.registry): title, description, where it
// applies (global / per workspace / per MO2 profile), when it applies (at once / with keep-or-revert / at the next
// start), its current and default value and a reset. A page widget (type "panel") is given as `panel`.
Rectangle {
    id: root
    required property var setting      // descriptor
    property Component panel: null     // content of a "panel" setting
    property bool highlighted: false   // reached from the search
    readonly property int rev: AppSettings.revision
    readonly property var current: { root.rev; return AppSettings.value(setting.id) }
    readonly property bool atDefault: { root.rev; return AppSettings.isDefault(setting.id) }
    property string error: ""
    objectName: "setting:" + setting.id

    implicitHeight: content.implicitHeight + Theme.spacingLg * 2
    radius: Theme.radius
    color: highlighted ? Theme.accentSubtle : Theme.panelAlt
    border.color: highlighted ? Theme.accent : Theme.border
    border.width: highlighted ? 2 : 1

    function apply(value) {
        if (setting.apply === "confirm") AppSettings.beginPreview()
        root.error = AppSettings.setValue(setting.id, value)
    }
    function scopeText(scope) {
        return scope === "workspace" ? qsTr("Por espacio de trabajo") : scope === "mo2-profile" ? qsTr("Por perfil MO2") : qsTr("Global")
    }
    function applyText(apply) {
        return apply === "confirm" ? qsTr("Con vista previa: mantener o revertir") : apply === "restart" ? qsTr("Al reiniciar") : qsTr("Aplicación inmediata")
    }
    function defaultText() {
        const d = root.setting.default
        if (root.setting.type === "bool") return d ? qsTr("Sí") : qsTr("No")
        if (root.setting.type === "enum") {
            const o = (root.setting.options || []).find(x => x.value === d)
            return o ? o.label : String(d)
        }
        if (root.setting.type === "font") return qsTr("Segoe UI")
        return d + " " + (root.setting.unit || "")
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd
            Text {
                Layout.fillWidth: true
                text: root.setting.title
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.normal
                wrapMode: Text.WordWrap
            }
            Text {
                visible: !root.atDefault
                text: "● " + qsTr("cambiado")
                color: Theme.accent
                font.pixelSize: Theme.micro
                Accessible.name: qsTr("Valor distinto del predeterminado")
            }
            ToolButton {
                objectName: "resetSetting:" + root.setting.id
                visible: root.setting.backend !== "panel"
                enabled: !root.atDefault
                text: "↺"
                font.pixelSize: Theme.normal
                implicitHeight: Theme.controlHeight
                Accessible.name: qsTr("Restablecer %1").arg(root.setting.title)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Volver al predeterminado")
                onClicked: { root.error = ""; AppSettings.resetSetting(root.setting.id) }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.setting.description
            color: Theme.textDim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        // ---- the control of the setting's type
        Switch {
            visible: root.setting.type === "bool"
            objectName: "control:" + root.setting.id
            checked: root.current === true
            text: checked ? qsTr("Activado") : qsTr("Desactivado")
            Accessible.name: root.setting.title
            onToggled: root.apply(checked)
        }

        RowLayout {
            visible: root.setting.type === "int"
            Layout.fillWidth: true
            spacing: Theme.spacingMd
            Slider {
                objectName: "control:" + root.setting.id
                Layout.fillWidth: true
                from: root.setting.min ?? 0
                to: root.setting.max ?? 100
                stepSize: root.setting.step ?? 1
                snapMode: Slider.SnapAlways
                value: root.setting.type === "int" ? Number(root.current) : 0
                Accessible.name: root.setting.title
                onMoved: root.apply(Math.round(value))
            }
            Text {
                Layout.minimumWidth: Theme.px(64)
                horizontalAlignment: Text.AlignRight
                text: (root.setting.type === "int" ? Number(root.current) : "") + " " + (root.setting.unit || "")
                color: Theme.text
                font.pixelSize: Theme.normal
            }
        }

        ComboBox {
            visible: root.setting.type === "enum"
            objectName: "control:" + root.setting.id
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.px(360)
            model: root.setting.options || []
            textRole: "label"
            valueRole: "value"
            currentIndex: { root.rev; return (root.setting.options || []).findIndex(o => o.value === root.current) }
            Accessible.name: root.setting.title
            onActivated: root.apply(currentValue)
        }

        ColumnLayout {
            visible: root.setting.type === "font"
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            ComboBox {
                id: fontBox
                objectName: "control:" + root.setting.id
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.px(360)
                // Every installed family is listed only when the list opens: enumerating Windows' fonts the first
                // time takes seconds and would freeze the page.
                property bool loaded: false
                readonly property var families: [qsTr("Predeterminada (Segoe UI)")]
                    .concat(loaded ? Qt.fontFamilies() : (root.current ? [root.current] : []))
                model: root.setting.type === "font" ? families : []
                currentIndex: { root.rev; return root.current ? Math.max(0, families.indexOf(root.current)) : 0 }
                Accessible.name: root.setting.title
                popup.onAboutToShow: loaded = true
                onActivated: root.apply(currentIndex === 0 ? "" : currentText)
            }
            Text {
                text: qsTr("Vista previa: Lydia, Serana y Aela · 0123456789")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.large
                color: Theme.text
            }
        }

        Loader {
            Layout.fillWidth: true
            active: root.setting.type === "panel" && root.panel !== null
            visible: active
            sourceComponent: root.panel
        }

        Text {
            Layout.fillWidth: true
            visible: root.error.length > 0
            text: "⚠ " + root.error
            color: Theme.error
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        // Where and when it applies, and the default: never only a color.
        Text {
            Layout.fillWidth: true
            text: root.scopeText(root.setting.scope) + " · " + root.applyText(root.setting.apply)
                  + (root.setting.type !== "panel" ? " · " + qsTr("Predeterminado: %1").arg(root.defaultText()) : "")
            color: Theme.textMuted
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: Theme.advancedMode
            text: root.setting.id + " → " + root.setting.handler
            color: Theme.textMuted
            font.family: Theme.mono
            font.pixelSize: Theme.micro
            wrapMode: Text.WrapAnywhere
        }
    }
}
