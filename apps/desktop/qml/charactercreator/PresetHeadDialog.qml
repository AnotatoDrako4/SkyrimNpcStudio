import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Export Head readiness of a RaceMenu preset (studio.presetHeads): which Export Head NIF belongs to the preset and whether it can
// be a face source. Diagnosis only: nothing is copied, applied or exported from here, and every verdict carries the caveat that it
// was not seen inside Skyrim. A present NIF is never presented as a safe FaceGen.
Popup {
    id: dialog
    objectName: "presetHeadDialog"
    required property StudioController studio
    readonly property PresetHeadChecker checker: studio.presetHeads
    readonly property var report: checker.report
    readonly property string state: report.state ?? "none"
    readonly property string verdict: report.verdict ?? ""

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: parent ? Math.min(parent.width - 2 * Theme.spacingXl, Theme.px(760)) : Theme.px(700)
    height: parent ? Math.min(parent.height - 2 * Theme.spacingXl, Theme.px(640)) : Theme.px(560)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    background: Rectangle { color: Theme.bg; radius: Theme.radiusLarge; border.color: Theme.border }

    function toneOf(v) {
        return v === "READY_FOR_REVIEW" ? Theme.ok : v === "NEEDS_CHOICE" ? Theme.accent
             : v === "NEEDS_ASSET" ? Theme.warn : v === "" ? Theme.textDim : Theme.error
    }
    function textOf(v) {
        return v === "READY_FOR_REVIEW" ? qsTr("READY_FOR_REVIEW · lista para revisar")
             : v === "NEEDS_ASSET" ? qsTr("NEEDS_ASSET · falta un archivo")
             : v === "NEEDS_CHOICE" ? qsTr("NEEDS_CHOICE · elige una cabeza")
             : v === "FACEGEN_UNREADABLE" ? qsTr("FACEGEN_UNREADABLE · NIF dañado")
             : v === "HEAD_PART_MISMATCH" ? qsTr("HEAD_PART_MISMATCH · no encaja")
             : v === "UNSUPPORTED_TOPOLOGY" ? qsTr("UNSUPPORTED_TOPOLOGY · otra malla")
             : v
    }

    contentItem: ColumnLayout {
        spacing: 0
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Export Head del preset")

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: titleRow.implicitHeight + 2 * Theme.spacingMd
            color: Theme.header
            radius: Theme.radiusLarge
            RowLayout {
                id: titleRow
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                Text {
                    objectName: "presetHeadTitle"
                    Layout.fillWidth: true
                    text: qsTr("Export Head de «%1»").arg((dialog.report.preset ?? {}).name ?? "?")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.large
                    elide: Text.ElideRight
                }
                Button { objectName: "presetHeadClose"; text: qsTr("Cerrar"); onClicked: dialog.close() }
            }
        }

        ScrollView {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingSm
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: body.availableWidth
                spacing: Theme.spacingSm

                Text {
                    objectName: "presetHeadState"
                    Layout.fillWidth: true
                    visible: dialog.state !== "ready"
                    text: dialog.state === "indexing" ? qsTr("Leyendo las cabezas exportadas del perfil (SKSE\\Plugins\\CharGen)…")
                        : dialog.state === "checking" ? qsTr("Resolviendo las piezas del preset y buscando su cabeza…")
                        : dialog.state === "error" ? qsTr("No se pudo comprobar: %1").arg(dialog.report.error ?? "?")
                        : qsTr("Elige un preset.")
                    color: dialog.state === "error" ? Theme.warn : Theme.textDim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready"
                    spacing: Theme.spacingSm
                    StatusBadge {
                        objectName: "presetHeadVerdict"
                        text: dialog.textOf(dialog.verdict)
                        tone: dialog.toneOf(dialog.verdict)
                        Accessible.name: qsTr("Veredicto: %1").arg(text)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (dialog.report.codes ?? []).join(", ")
                        color: Theme.textMuted
                        font.pixelSize: Theme.micro
                        elide: Text.ElideRight
                    }
                }
                Text {
                    objectName: "presetHeadMessage"
                    Layout.fillWidth: true
                    visible: dialog.state === "ready"
                    text: dialog.report.message ?? ""
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready"
                    text: qsTr("UNVERIFIED_IN_SKYRIM: ningún veredicto de aquí se ha visto dentro del juego, y «hay un NIF» no significa «FaceGen seguro».")
                    color: Theme.warn
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }

                // The head that was paired.
                Rectangle {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready" && dialog.report.chosen !== undefined
                    implicitHeight: chosenColumn.implicitHeight + 12
                    color: Theme.panel
                    radius: Theme.radiusSmall
                    border.color: Theme.border
                    ColumnLayout {
                        id: chosenColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Text {
                            objectName: "presetHeadChosen"
                            Layout.fillWidth: true
                            text: qsTr("Cabeza emparejada: %1").arg((dialog.report.chosen ?? {}).path ?? "")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Modificada: %1 · vértices de la cara: %2 (pieza del preset: %3)")
                                      .arg(((dialog.report.chosen ?? {}).modified ?? "").replace("T", " ")).arg((dialog.report.chosen ?? {}).faceVertices ?? "?")
                                      .arg(dialog.report.faceVertices ?? qsTr("sin comprobar"))
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: ((dialog.report.chosen ?? {}).missingTextures ?? []).length > 0
                            text: qsTr("Texturas que faltan en el perfil: ") + ((dialog.report.chosen ?? {}).missingTextures ?? []).join(", ")
                            color: Theme.warn
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                // Candidates and the manual choice.
                Text {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready" && (dialog.report.ranked ?? []).length > 0
                    text: qsTr("Cabezas que podrían ser de este preset")
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: Theme.small
                }
                Repeater {
                    model: dialog.state === "ready" ? (dialog.report.ranked ?? []) : []
                    delegate: Rectangle {
                        id: rankedRow
                        required property var modelData
                        objectName: "presetHeadRanked_" + modelData.index
                        Layout.fillWidth: true
                        implicitHeight: rankedLayout.implicitHeight + 10
                        color: Theme.panel
                        radius: Theme.radiusSmall
                        border.color: (dialog.report.chosen ?? {}).index === modelData.index ? Theme.accent : Theme.border
                        RowLayout {
                            id: rankedLayout
                            anchors.fill: parent
                            anchors.margins: 5
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { Layout.fillWidth: true; text: rankedRow.modelData.path; color: Theme.text; font.pixelSize: Theme.micro; wrapMode: Text.WrapAnywhere }
                                Text { Layout.fillWidth: true; text: rankedRow.modelData.reason; color: Theme.textDim; font.pixelSize: Theme.micro; wrapMode: Text.WordWrap }
                            }
                            Button {
                                objectName: "presetHeadUse_" + rankedRow.modelData.index
                                text: qsTr("Usar esta")
                                enabled: (dialog.report.chosen ?? {}).index !== rankedRow.modelData.index
                                Accessible.name: qsTr("Usar la cabeza %1 para este preset").arg(rankedRow.modelData.path)
                                onClicked: dialog.checker.choose(rankedRow.modelData.index)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready" && (dialog.report.allHeads ?? []).length > 0
                    ComboBox {
                        id: anyHead
                        objectName: "presetHeadAny"
                        Layout.fillWidth: true
                        textRole: "label"
                        model: dialog.report.allHeads ?? []
                        Accessible.name: qsTr("Cualquier cabeza exportada del perfil")
                    }
                    Button {
                        objectName: "presetHeadPick"
                        text: qsTr("Elegir a mano")
                        enabled: anyHead.currentIndex >= 0
                        onClicked: dialog.checker.choose((dialog.report.allHeads ?? [])[anyHead.currentIndex].index)
                    }
                    Button {
                        objectName: "presetHeadClear"
                        text: qsTr("Quitar elección")
                        visible: dialog.report.manual === true
                        onClicked: dialog.checker.clearChoice()
                    }
                }

                Text {
                    objectName: "presetHeadChoiceNote"
                    Layout.fillWidth: true
                    visible: dialog.state === "ready" && (dialog.report.manual === true || dialog.report.manualStale === true || (dialog.report.persistError ?? "").length > 0)
                    text: (dialog.report.persistError ?? "").length > 0 ? "⚠ " + dialog.report.persistError
                        : dialog.report.manualStale === true ? qsTr("La cabeza que habías elegido antes ya no está en el perfil (%1): se ignora y se vuelve a emparejar por contenido.").arg(dialog.report.manualHead ?? "")
                        : qsTr("Elección manual recordada para este preset en este perfil (la puedes quitar con «Quitar elección»).")
                    color: (dialog.report.persistError ?? "").length > 0 || dialog.report.manualStale === true ? Theme.warn : Theme.textDim
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: dialog.state === "ready"
                    text: qsTr("Piezas del preset: %1 resueltas, %2 sin resolver · pieza Face: %3")
                              .arg((dialog.report.parts ?? []).length).arg((dialog.report.unresolved ?? []).length).arg(dialog.report.facePart || qsTr("ninguna"))
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Solo diagnóstico: no se copia, aplica ni exporta nada desde aquí, y la elección manual solo se recuerda (en un archivo del estudio, por perfil). Cabezas leídas del "
                             + "perfil: %1 (%2 ilegibles).").arg((dialog.checker.heads.count ?? 0)).arg((dialog.checker.heads.unreadable ?? 0))
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
