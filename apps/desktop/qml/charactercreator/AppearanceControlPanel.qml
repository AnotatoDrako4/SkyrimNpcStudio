import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Appearance control map of the selected NPC (StudioController.appearanceControl): which plugin and
// MO2 mod controls each part of its look, what it overrides, and whether the record and the FaceGen
// files fit together. Read-only: suggestions are never applied automatically. The one assisted fix
// («Usar el rostro de X») only opens the Easy Mod Creator, which writes a separate patch mod.
ScrollView {
    id: root
    objectName: "appearanceControlPanel"
    required property StudioController studio
    readonly property var map: studio.appearanceControl
    readonly property bool hasMap: (map.components || []).length > 0
    readonly property var resolution: map.resolution || null
    // The creator opens the assistant (it owns it); the plan is already prepared.
    signal resolutionRequested()
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
    // Individual review (D1) of an ambiguous resolution.
    function reviewStateText(state) {
        return state === "approved" ? qsTr("aprobada para las entradas actuales")
             : state === "rejected" ? qsTr("rechazada (no se exporta)")
             : state === "deferred" ? qsTr("aplazada (no se exporta)")
             : state === "stale" ? qsTr("caducada: las entradas cambiaron")
             : qsTr("pendiente")
    }
    function reviewStateColor(state) {
        return state === "approved" ? Theme.ok : state === "rejected" ? Theme.error : Theme.warn
    }
    function reviewSummary() {
        const rows = (root.scan.rows || []).filter(r => r.requiresReview === true)
        if (rows.length === 0) return ""
        const count = s => rows.filter(r => r.reviewState === s).length
        return qsTr("%1 casos ambiguos con revisión individual: %2 pendientes, %3 aprobados, %4 rechazados, %5 aplazados, %6 caducados")
            .arg(rows.length).arg(count("pending")).arg(count("approved")).arg(count("rejected")).arg(count("deferred")).arg(count("stale"))
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
            font.pixelSize: Theme.fs(13)
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: !root.hasMap
            text: qsTr("Selecciona un NPC para ver qué mod controla cada parte de su apariencia.")
            color: Theme.textDim
            font.pixelSize: Theme.small
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
                    font.pixelSize: Theme.normal
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Datos faciales: %1 (mod «%2»)").arg(root.map.facePlugin || "?").arg(root.map.faceMod || "?")
                          + (root.map.faceVia ? qsTr(", copiados por %1").arg(root.map.faceVia) : "")
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    text: root.map.deviation !== undefined && root.map.deviation !== null
                        ? qsTr("FaceGen frente al registro: desviación medida %1 unidades").arg(Number(root.map.deviation).toFixed(3))
                        : root.studio.appearance.analysing ? qsTr("Midiendo el FaceGen frente al registro…")
                        : qsTr("FaceGen frente al registro: no comparable (otras formas o sin FaceGen)")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
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
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.consequence || "").length > 0
                        text: qsTr("En el juego: ") + modelData.consequence
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.suggestion || "").length > 0
                        text: "→ " + modelData.suggestion
                        color: Theme.text
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ---- assisted resolution («Usar el rostro de X»)
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: root.hasMap && root.resolution !== null
            implicitHeight: resolutionColumn.implicitHeight + 14
            color: Theme.panelAlt
            radius: 4
            border.color: Theme.accent
            border.width: 1

            ColumnLayout {
                id: resolutionColumn
                anchors.fill: parent
                anchors.margins: 7
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Resolución asistida")
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: Theme.small
                }
                Text {
                    objectName: "faceResolutionExplanation"
                    Layout.fillWidth: true
                    text: root.resolution ? (root.resolution.explanation || root.resolution.detail) : ""
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
                // Consequences before exporting: what stays, what stops applying, what is still pending.
                Repeater {
                    model: root.resolution ? [].concat(
                        (root.resolution.keeps || []).map(l => qsTr("Se conserva: ") + l),
                        (root.resolution.replaced || []).map(l => qsTr("Deja de aplicarse: ") + l),
                        (root.resolution.pending || []).map(l => qsTr("Sigue pendiente: ") + l),
                        (root.resolution.alternatives || []).map(l => qsTr("Otro rostro: ") + l)) : []
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: "· " + modelData
                        color: modelData.startsWith(qsTr("Sigue pendiente")) ? Theme.warn : Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.resolution ? qsTr("Campos: %1 · se carga después de %2")
                                              .arg(root.resolution.fields.join(", ")).arg(root.resolution.loadAfter.join(" y ")) : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WrapAnywhere
                }
                // Individual review (D1): this patch would discard a mod's own face edits. It is exportable only
                // after a human decision for this NPC, bound to the digest of the inputs shown here.
                Rectangle {
                    objectName: "faceReviewPanel"
                    Layout.fillWidth: true
                    visible: root.resolution !== null && root.resolution.requiresReview === true
                    implicitHeight: reviewColumn.implicitHeight + 12
                    color: Theme.panel
                    radius: 3
                    border.color: root.reviewStateColor(root.resolution ? root.resolution.reviewState : "")
                    border.width: 1
                    ColumnLayout {
                        id: reviewColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        Text {
                            objectName: "faceReviewState"
                            Layout.fillWidth: true
                            text: qsTr("Revisión individual: %1").arg(root.reviewStateText(root.resolution ? root.resolution.reviewState : ""))
                            color: root.reviewStateColor(root.resolution ? root.resolution.reviewState : "")
                            font.bold: true
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: root.resolution ? (root.resolution.reviewReason || "") : ""
                            color: Theme.text
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !!(root.resolution && root.resolution.reviewDecidedAt)
                            text: root.resolution && root.resolution.reviewDecidedAt
                                  ? qsTr("Última decisión: %1 (%2)%3").arg(root.resolution.reviewDecision).arg(root.resolution.reviewDecidedAt)
                                        .arg(root.resolution.reviewNotes ? " — " + root.resolution.reviewNotes : "")
                                  : ""
                            color: Theme.textDim
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WordWrap
                        }
                        TextField {
                            id: reviewNotes
                            objectName: "faceReviewNotes"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Notas de la revisión (opcional)")
                            font.pixelSize: Theme.small
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Button {
                                objectName: "faceReviewApprove"
                                Layout.fillWidth: true
                                text: qsTr("Aprobar")
                                enabled: !root.studio.appearance.exporting
                                onClicked: if (root.studio.setReviewDecision("approve", reviewNotes.text)) reviewNotes.text = ""
                            }
                            Button {
                                objectName: "faceReviewReject"
                                Layout.fillWidth: true
                                text: qsTr("Rechazar")
                                enabled: !root.studio.appearance.exporting
                                onClicked: if (root.studio.setReviewDecision("reject", reviewNotes.text)) reviewNotes.text = ""
                            }
                            Button {
                                objectName: "faceReviewDefer"
                                Layout.fillWidth: true
                                text: qsTr("Aplazar")
                                enabled: !root.studio.appearance.exporting
                                onClicked: if (root.studio.setReviewDecision("defer", reviewNotes.text)) reviewNotes.text = ""
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Decide solo por este NPC y estas entradas (huella %1…). Si cambian el orden de carga, el FaceGen "
                                     + "efectivo o la propuesta, la aprobación caduca. Registro: %2")
                                      .arg(root.resolution && root.resolution.inputDigest ? root.resolution.inputDigest.slice(0, 12) : "?")
                                      .arg(root.studio.reviewBookPath)
                            color: Theme.textMuted
                            font.pixelSize: Theme.micro
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Button {
                    objectName: "faceResolutionButton"
                    Layout.fillWidth: true
                    text: root.resolution ? root.resolution.label : ""
                    enabled: !root.studio.appearance.exporting
                    contentItem: Text {
                        text: parent.text
                        color: Theme.text
                        font.pixelSize: Theme.small
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: if (root.studio.prepareFaceResolution()) root.resolutionRequested()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Abre el asistente de Easy Mod: comprobaciones y creación de un mod nuevo en la carpeta que elijas. "
                             + "No cambia el orden de carga, MO2 ni Skyrim/Data.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- head <-> body skin (an estimate of the seam at the neck, from the textures the studio draws)
        SkinMatchCard {
            Layout.fillWidth: true
            visible: root.hasMap
            studio: root.studio
        }

        // ---- every source the profile offers for this face, and the A/B comparison (read-only)
        AppearanceSourceSection {
            Layout.fillWidth: true
            visible: root.hasMap
            studio: root.studio
        }

        // ---- parts
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            visible: root.hasMap
            text: qsTr("Partes de la apariencia")
            color: Theme.accent
            font.bold: true
            font.pixelSize: Theme.normal
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
                        font.pixelSize: Theme.small
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.kind === "record"
                            ? modelData.plugin + "  «" + modelData.mod + "»"
                            : (modelData.mod ? "«" + modelData.mod + "» " : "") + root.kindText(modelData.kind)
                        color: modelData.kind === "missing" ? Theme.warn : Theme.textDim
                        font.pixelSize: Theme.small
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    visible: (modelData.via || "").length > 0
                    text: qsTr("copiado por: ") + (modelData.via || "")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    visible: (modelData.overridden || []).length > 0
                    text: qsTr("anula a: ") + (modelData.overridden || []).join(", ")
                    color: Theme.textMuted
                    font.pixelSize: Theme.micro
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
            font.pixelSize: Theme.normal
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
                font.pixelSize: Theme.micro
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
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: "scanReviewSummary"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: text.length > 0
            text: root.reviewSummary()
            color: Theme.warn
            font.pixelSize: Theme.micro
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
                        font.pixelSize: Theme.small
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("datos faciales «%1» · FaceGen «%2» · %3").arg(modelData.faceMod).arg(modelData.faceGenMod || qsTr("ninguno"))
                                  .arg(modelData.codes.join(", "))
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: modelData.requiresReview === true
                        text: qsTr("Caso ambiguo · revisión %1").arg(root.reviewStateText(modelData.reviewState))
                        color: root.reviewStateColor(modelData.reviewState)
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
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
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }
    }
}
