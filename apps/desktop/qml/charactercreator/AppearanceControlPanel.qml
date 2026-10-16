import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Appearance control map of the selected NPC (StudioController.appearanceControl): which plugin and
// MO2 mod controls each part of its look, what it overrides, and whether the record and the FaceGen
// files fit together. Read-only: suggestions are never applied automatically.
ScrollView {
    id: root
    objectName: "appearanceControlPanel"
    required property StudioController studio
    readonly property var map: studio.appearanceControl
    readonly property bool hasMap: (map.components || []).length > 0
    readonly property var scan: studio.appearanceScan
    property int scanRowsShown: 50
    contentWidth: availableWidth
    clip: true

    function scanStatus() {
        const s = root.scan
        if (s.stage === undefined) return qsTr("Revisa el FaceGen y el registro de cada NPC único (sin la medición facial).")
        if (s.stage === "listing") return qsTr("Listando NPC… %1").arg(s.listed)
        if (s.stage === "analysing")
            return qsTr("Analizados %1 de %2 (quedan %3 con varias versiones)").arg(s.analysed).arg(s.total).arg(s.pending)
        const counts = s.counts || {}
        const head = s.stage === "done" ? qsTr("Terminado") : s.stage === "cancelled" ? qsTr("Cancelado") : qsTr("Falló")
        return qsTr("%1: %2 NPC en %3 s — %4 con avisos, %5 con notas%6").arg(head).arg(s.analysed)
            .arg((s.elapsedMs / 1000).toFixed(1)).arg(counts.warning || 0).arg(counts.info || 0)
            .arg(s.errors > 0 ? qsTr(", %1 errores").arg(s.errors) : "")
    }
    function scanCodes() {
        const codes = root.scan.codes || {}
        return Object.keys(codes).filter(k => k !== "FaceSourcesConsistent" && k !== "FaceGenFromOtherModMatches")
            .sort((a, b) => codes[b] - codes[a]).map(k => k + " " + codes[k]).join(" · ")
    }
    onScanChanged: if (scan.stage === "listing") scanRowsShown = 50

    function severityText(severity) {
        return severity === "warning" ? qsTr("Aviso") : severity === "info" ? qsTr("Nota") : qsTr("Correcto")
    }
    function severityColor(severity) {
        return severity === "warning" ? Theme.warn : severity === "info" ? Theme.textDim : Theme.ok
    }
    function kindText(kind) {
        return kind === "loose" ? qsTr("archivo suelto") : kind === "bsa" ? qsTr("dentro de un BSA")
             : kind === "missing" ? qsTr("no existe en el perfil") : ""
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 8
            text: qsTr("Quién controla la apariencia")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 13
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: !root.hasMap
            text: qsTr("Selecciona un NPC para ver qué mod controla cada parte de su apariencia.")
            color: Theme.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        // ---- verdict
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: root.hasMap
            implicitHeight: verdict.implicitHeight + 14
            color: Theme.panelAlt
            radius: 4
            border.color: root.severityColor(root.map.worst)
            border.width: 1

            ColumnLayout {
                id: verdict
                anchors.fill: parent
                anchors.margins: 7
                spacing: 3
                Text {
                    objectName: "appearanceControlVerdict"
                    Layout.fillWidth: true
                    text: root.map.worst === "warning" ? qsTr("Hay conflictos que afectan al rostro")
                        : root.map.worst === "info" ? qsTr("Sin conflictos graves (ver notas)")
                        : qsTr("Registro y FaceGen encajan")
                    color: root.severityColor(root.map.worst)
                    font.bold: true
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Datos faciales: %1 (mod «%2»)").arg(root.map.facePlugin || "?").arg(root.map.faceMod || "?")
                    color: Theme.text
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    text: root.map.deviation !== undefined && root.map.deviation !== null
                        ? qsTr("FaceGen frente al registro: desviación medida %1 unidades").arg(Number(root.map.deviation).toFixed(3))
                        : root.studio.appearance.analysing ? qsTr("Midiendo el FaceGen frente al registro…")
                        : qsTr("FaceGen frente al registro: no comparable (otras formas o sin FaceGen)")
                    color: Theme.textDim
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- findings
        Repeater {
            model: root.hasMap ? (root.map.findings || []) : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                implicitHeight: findingColumn.implicitHeight + 12
                color: Theme.panel
                radius: 4
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: findingColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: root.severityText(modelData.severity) + " · " + modelData.message
                        color: root.severityColor(modelData.severity)
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.consequence || "").length > 0
                        text: qsTr("En el juego: ") + modelData.consequence
                        color: Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.suggestion || "").length > 0
                        text: "→ " + modelData.suggestion
                        color: Theme.text
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ---- parts
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            visible: root.hasMap
            text: qsTr("Partes de la apariencia")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 12
        }
        Repeater {
            model: root.hasMap ? root.map.components : []
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                spacing: 1
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: modelData.label
                        color: Theme.text
                        font.pixelSize: 11
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.kind === "record"
                            ? modelData.plugin + "  «" + modelData.mod + "»"
                            : (modelData.mod ? "«" + modelData.mod + "» " : "") + root.kindText(modelData.kind)
                        color: modelData.kind === "missing" ? Theme.warn : Theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    visible: (modelData.overridden || []).length > 0
                    text: qsTr("anula a: ") + (modelData.overridden || []).join(", ")
                    color: Theme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        // ---- every unique NPC of the profile
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.topMargin: 6
            text: qsTr("Todo el perfil")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Button {
                objectName: "appearanceScanButton"
                text: root.scan.running ? qsTr("Cancelar") : qsTr("Analizar NPC únicos")
                enabled: root.studio.phase === StudioController.Ready
                onClicked: root.scan.running ? root.studio.cancelAppearanceScan() : root.studio.scanAppearanceControl()
            }
            Text {
                objectName: "appearanceScanStatus"
                Layout.fillWidth: true
                text: root.scanStatus()
                color: Theme.textDim
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: root.scanCodes().length > 0
            text: root.scanCodes()
            color: Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: (root.scan.rows || []).slice(0, root.scanRowsShown)
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                implicitHeight: rowColumn.implicitHeight + 8
                color: rowArea.containsMouse ? Theme.panelAlt : Theme.panel
                radius: 3
                ColumnLayout {
                    id: rowColumn
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: root.severityText(modelData.worst) + " · " + modelData.name + "  [" + modelData.formKey + "]"
                        color: root.severityColor(modelData.worst)
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("datos faciales «%1» · FaceGen «%2» · %3").arg(modelData.faceMod).arg(modelData.faceGenMod || qsTr("ninguno"))
                                  .arg(modelData.codes.join(", "))
                        color: Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                    }
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.studio.selectNpc(modelData.formKey)
                }
            }
        }
        Button {
            Layout.leftMargin: 8
            visible: (root.scan.rows || []).length > root.scanRowsShown
            text: qsTr("Mostrar más (%1 restantes)").arg((root.scan.rows || []).length - root.scanRowsShown)
            flat: true
            onClicked: root.scanRowsShown += 50
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            visible: root.hasMap
            text: qsTr("Análisis en archivos del perfil (registro, MO2 y FaceGen), sin cambiar nada: el orden de carga y las "
                     + "prioridades de MO2 no se tocan. Pendiente de verificación dentro de Skyrim.")
            color: Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
