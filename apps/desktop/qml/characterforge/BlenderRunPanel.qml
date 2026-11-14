import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// One Blender run (CharacterForgeViewModel.blenderRun, from ForgeBlenderBridge): the step it is on, how it ended and,
// when Blender crashed or hung AFTER writing a verified result, the explicit choice to use or drop that result.
// Process exit, the script's report, the file check and the marker are shown apart: none of them alone means success.
ColumnLayout {
    id: root
    required property CharacterForgeViewModel forge
    // Which runs this panel speaks for: "preview" (GLB for the viewer) and/or "inspect" / "exportNif" (Skyrim jobs).
    property var kinds: ["preview", "inspect", "exportNif"]
    property bool showProgress: true  // false where the host already draws the run's progress
    readonly property var info: forge.blenderRun || ({})
    readonly property var run: info.run || ({})
    readonly property bool mine: kinds.indexOf(info.kind) >= 0
    readonly property bool running: mine && info.state === "running"
    readonly property bool recoverable: mine && info.state === "recoverable"
    readonly property bool failed: mine && info.state === "failed"
    readonly property bool recovered: mine && info.state === "done" && run.decision === "recovered"
    property real startedAt: 0
    property real elapsed: 0
    objectName: "forgeBlenderRunPanel"
    // Hosts bind their own visibility to this (a child's "visible" is false while its parent is hidden).
    readonly property bool shows: (running && showProgress) || recoverable || failed || recovered
    visible: shows
    spacing: Theme.spacingSm

    readonly property var stepNames: ({
        version: qsTr("comprobando Blender"), open: qsTr("abriendo el modelo"), textures: qsTr("buscando texturas"),
        prune: qsTr("limpiando la escena"), reduce: qsTr("aligerando la vista previa"), images: qsTr("reduciendo texturas"),
        export: qsTr("exportando"), verify: qsTr("verificando el archivo"), start: qsTr("iniciando"),
        load: qsTr("abriendo la copia"), inspect: qsTr("inspeccionando"), prepare: qsTr("preparando la copia"),
        readback: qsTr("releyendo el NIF"), done: qsTr("cerrando Blender")
    })
    readonly property var kindNames: ({ preview: qsTr("Vista previa GLB"), inspect: qsTr("Análisis Skyrim"), exportNif: qsTr("Exportación NIF") })

    onRunningChanged: if (running) { startedAt = Date.now(); elapsed = 0 }
    Timer {
        interval: 250
        repeat: true
        running: root.running
        onTriggered: root.elapsed = (Date.now() - root.startedAt) / 1000
    }

    function retry() {
        if (info.kind === "preview") forge.convertSelectedModelWithBlender()
        else if (info.kind === "inspect") forge.analyzeSkyrimCompatibility()
        else if (info.kind === "exportNif") forge.exportSkyrimNif()
    }

    // ---- running: real step and fraction reported by the script --------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.running && root.showProgress
        spacing: Theme.spacingXs
        ProgressBar {
            objectName: "forgeBlenderRunProgress"
            Layout.fillWidth: true
            from: 0; to: 1
            value: root.info.fraction || 0
            indeterminate: !root.info.fraction
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                color: Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
                text: qsTr("%1 · Blender: %2 · %3 s")
                    .arg(root.kindNames[root.info.kind] || "")
                    .arg(root.stepNames[root.info.step] || root.info.step || "")
                    .arg(root.elapsed.toFixed(0))
            }
            Button {
                text: qsTr("Cancelar")
                onClicked: root.forge.cancelOperation()
            }
        }
    }

    // ---- recoverable / failed / recovered --------------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        visible: root.recoverable || root.failed || root.recovered
        implicitHeight: box.implicitHeight + Theme.spacingLg * 2
        radius: Theme.radius
        color: root.failed ? Theme.errorDim : Theme.warnDim
        border.color: root.failed ? Theme.error : Theme.warn
        border.width: 1
        ColumnLayout {
            id: box
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingSm
            Text {
                Layout.fillWidth: true
                objectName: "forgeBlenderRunTitle"
                font.bold: true
                color: Theme.text
                wrapMode: Text.WordWrap
                text: root.recoverable ? qsTr("Blender terminó mal después de generar un resultado verificado")
                    : root.recovered ? qsTr("Resultado recuperado de una ejecución de Blender que terminó mal")
                    : qsTr("%1: no se pudo completar").arg(root.kindNames[root.info.kind] || qsTr("Blender"))
            }
            Text {
                Layout.fillWidth: true
                objectName: "forgeBlenderRunMessage"
                color: Theme.text
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                text: root.info.message || root.run.message || ""
            }
            // The four facts, side by side, so a reader never has to infer them from a log.
            GridLayout {
                Layout.fillWidth: true
                visible: !!root.run.exit
                columns: 2
                columnSpacing: Theme.spacingLg
                rowSpacing: 2
                Text { color: Theme.textDim; font.pixelSize: Theme.small; text: qsTr("Proceso") }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: Theme.small
                    text: ({ normal: qsTr("terminó con código 0"), nonZero: qsTr("código de salida %1").arg(root.run.exitCode !== undefined ? root.run.exitCode : ""),
                             crashed: qsTr("fallo %1 %2").arg(root.run.exitHex || "").arg(root.run.exitMeaning ? "(" + root.run.exitMeaning + ")" : ""),
                             timedOut: qsTr("superó el tiempo máximo"), hungAfterResult: qsTr("no se cerró tras terminar; detenido"),
                             cancelled: qsTr("cancelado"), failedToStart: qsTr("no se pudo iniciar") })[root.run.exit] || root.run.exit || ""
                }
                Text { color: Theme.textDim; font.pixelSize: Theme.small; text: qsTr("Marca de fin") }
                Text { color: Theme.text; font.pixelSize: Theme.small; text: root.run.markerOk ? qsTr("presente") : qsTr("ausente") }
                Text { color: Theme.textDim; font.pixelSize: Theme.small; text: qsTr("Informe") }
                Text {
                    color: Theme.text; font.pixelSize: Theme.small
                    text: ({ ok: qsTr("correcto"), error: qsTr("error del script"), missing: qsTr("no escrito"), invalid: qsTr("no válido") })[root.run.report] || ""
                }
                Text { color: Theme.textDim; font.pixelSize: Theme.small; text: qsTr("Archivo") }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: Theme.small
                    text: ({ verified: qsTr("verificado (tamaño, cabecera y SHA-256)"), notApplicable: qsTr("no aplica (solo informe)"),
                             missing: qsTr("no existe"), truncated: qsTr("incompleto"), hashMismatch: qsTr("no coincide con el informe"),
                             invalid: qsTr("no válido") })[root.run.artifact] || ""
                }
                Text { color: Theme.textDim; font.pixelSize: Theme.small; text: qsTr("Duración"); visible: root.run.seconds !== undefined }
                Text { color: Theme.text; font.pixelSize: Theme.small; visible: root.run.seconds !== undefined; text: qsTr("%1 s").arg(Number(root.run.seconds || 0).toFixed(1)) }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Button {
                    objectName: "forgeBlenderRecoverButton"
                    visible: root.recoverable
                    highlighted: true
                    text: qsTr("Usar el resultado verificado")
                    onClicked: root.forge.acceptRecoveredBlenderResult()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Se añade con la advertencia guardada en el proyecto; Character Forge vuelve a validarlo antes de usarlo.")
                }
                Button {
                    objectName: "forgeBlenderDiscardButton"
                    visible: root.recoverable
                    text: qsTr("Descartar")
                    onClicked: root.forge.discardRecoveredBlenderResult()
                }
                Button {
                    visible: root.failed && root.run.exit !== "failedToStart" && !root.forge.busy
                    text: qsTr("Reintentar")
                    onClicked: root.retry()
                }
                Button {
                    visible: root.failed && (root.run.exit === "failedToStart" || String(root.info.message || "").indexOf("Blender 4 o 5") >= 0)
                    text: qsTr("Detectar Blender de nuevo")
                    onClicked: root.forge.detectBlender()
                }
                Button {
                    visible: !!root.run.crashLog
                    text: qsTr("Copiar ruta del informe de fallo")
                    onClicked: { clip.text = root.run.crashLog; clip.selectAll(); clip.copy() }
                }
            }
            // Technical details for a bug report: the end of Blender's stderr.
            ColumnLayout {
                Layout.fillWidth: true
                visible: !!root.run.stderrTail && (root.failed || root.recoverable)
                spacing: 2
                CheckBox {
                    id: detailsToggle
                    text: qsTr("Detalles técnicos (stderr de Blender)")
                    font.pixelSize: Theme.small
                }
                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(160, implicitHeight)
                    visible: detailsToggle.checked
                    readOnly: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: Theme.mono
                    font.pixelSize: Theme.micro
                    text: root.run.stderrTail || ""
                }
            }
        }
    }
    TextEdit { id: clip; visible: false }
}
