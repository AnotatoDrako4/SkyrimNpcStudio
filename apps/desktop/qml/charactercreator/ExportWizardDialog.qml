import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

// Easy Mod Creator: a guided export over the three workspaces (no editor of its own).
// 1 Personaje · 2 Modificación · 3 Vista previa · 4 Comprobación · 5 Crear mod.
// What can be exported, the export mode and every check come from AppearanceEditor.easyModPlan
// (sns::appearance::plan_easy_mod); the mod is written by the existing exporters (Outfit Studio
// for mode A, Character Creator for modes B and C), which verify it again in the worker.
Dialog {
    id: root
    required property StudioController studio
    readonly property var plan: studio.appearance.easyModPlan
    readonly property var summary: studio.npc.summary || ({})
    readonly property var compat: studio.appearance.faceCompatibility
    property int step: 0
    property bool waitingResult: false
    property var modResult: null
    property string failure: ""
    property var failureIssues: []

    readonly property var stepNames: [qsTr("Personaje"), qsTr("Modificación"), qsTr("Vista previa"), qsTr("Comprobación"), qsTr("Crear mod")]
    readonly property bool busy: studio.appearance.exporting || studio.outfit.exporting
    // Plugins the mod will need at least: the NPC's winning plugin (its override copies that record)
    // and, for an outfit change, the plugins of the pieces. The worker lists the exact masters.
    readonly property var dependencies: {
        var out = []
        var add = function (plugin, provider) {
            if (!plugin || plugin.toLowerCase() === "skyrim.esm") return
            for (var i = 0; i < out.length; ++i) if (out[i].plugin.toLowerCase() === plugin.toLowerCase()) return
            out.push({ plugin: plugin, provider: provider || "" })
        }
        var prov = compat.provenance || {}
        add(prov.winningPlugin || summary.winningPlugin, prov.pluginProvider)
        // A face resolution loads after the plugin it copies from and the winner (declared masters).
        var after = plan.loadAfter || []
        for (var k = 0; k < after.length; ++k) add(after[k], studio.providerOf(after[k]))
        var outfitChange = (plan.changes || []).some(function (c) { return c.key === "outfit" })
        if (outfitChange) {
            var pieces = studio.outfit.pieces || []
            for (var j = 0; j < pieces.length; ++j) add(pieces[j].plugin, "")
        }
        return out
    }

    // Not modal and not dimmed: the viewport stays visible next to the assistant (step 3).
    modal: false
    dim: false
    parent: Overlay.overlay
    // Sized from the window, not the overlay: the overlay can still be 0×0 when the window changes
    // size before it is first exposed, which collapsed the assistant to a negative height.
    readonly property real windowWidth: ApplicationWindow.window ? ApplicationWindow.window.width : parent.width
    readonly property real windowHeight: ApplicationWindow.window ? ApplicationWindow.window.height : parent.height
    x: Math.max(16, windowWidth - width - 24)
    y: 64
    // About a third of the window (420–600 px): on 1280 px the viewport still shows the NPC for step 3.
    width: Math.min(600, Math.max(420, windowWidth * 0.36), windowWidth - 32)
    height: Math.max(320, Math.min(windowHeight - 96, 680))
    title: qsTr("Crear un mod — asistente")

    Settings {
        id: prefs
        category: "easy_mod_creator"
        property string lastDirectory: ""
        property string author: ""
    }

    onAboutToShow: {
        step = 0
        modResult = null
        failure = ""
        failureIssues = []
        waitingResult = false
        if (root.plan.mode === "face-resolution")
            pluginName.text = root.plan.suggestedPluginName
        else if (pluginName.text.length === 0)
            pluginName.text = root.studio.outfit.suggestedPluginName.length > 0 ? root.studio.outfit.suggestedPluginName : "SNS_NpcMod.esp"
        if (outputDir.text.length === 0)
            outputDir.text = prefs.lastDirectory.length > 0 ? prefs.lastDirectory : root.studio.outfit.defaultExportDirectory
        if (author.text.length === 0) author.text = prefs.author
    }
    onClosed: {
        if (root.studio.scene.original) root.studio.scene.original = false
        // The resolution was chosen for this session of the assistant; the editors' plan comes back.
        if (!root.busy) root.studio.appearance.clearFaceResolution()
    }

    Connections {
        target: root.studio.appearance
        function onExportFinished(success, message, modDir, zipPath) {
            if (!root.waitingResult) return
            root.waitingResult = false
            if (success) root.modResult = root.studio.appearance.lastExport
            else { root.failure = message; root.failureIssues = [] }
        }
    }
    Connections {
        target: root.studio.outfit
        function onExported(r) {
            if (!root.waitingResult) return
            root.waitingResult = false
            root.modResult = r
        }
        function onExportFailed(message, issues) {
            if (!root.waitingResult) return
            root.waitingResult = false
            root.failure = message
            root.failureIssues = issues
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Carpeta de destino (fuera de Skyrim y de MO2)")
        onAccepted: outputDir.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{3}/, ""))
    }

    function stateIcon(state) {
        return state === "ok" ? "✓" : state === "warning" ? "⚠" : state === "waiting" ? "◷" : state === "blocked" ? "✕" : "•"
    }
    function stateColor(state) {
        return state === "ok" ? Theme.ok : state === "warning" ? Theme.warn : state === "blocked" ? Theme.error : Theme.textDim
    }
    function createMod() {
        var name = pluginName.text.trim()
        var dir = outputDir.text.trim()
        prefs.lastDirectory = dir
        prefs.author = author.text.trim()
        root.modResult = null
        root.failure = ""
        root.failureIssues = []
        root.waitingResult = true
        if (root.plan.mode === "outfit-patch")
            root.studio.outfit.exportPatch(name, dir, esl.checked, zip.checked, description.text, false, false, false, "", author.text)
        else if (root.plan.mode === "face-overhaul")
            root.studio.appearance.exportFaceOverhaul(name, dir, esl.checked, zip.checked, author.text, description.text)
        else if (root.plan.mode === "complete-overhaul")
            root.studio.appearance.exportCompleteOverhaul(name, dir, esl.checked, zip.checked, author.text, description.text)
        else if (root.plan.mode === "face-resolution")
            root.studio.appearance.exportFaceResolution(name, dir, esl.checked, zip.checked, author.text, description.text)
        else
            root.waitingResult = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---- steps
        // Narrow assistant (small windows): only the current step keeps its name, the others show
        // their number (with the name as tooltip), so the bar never pushes the page out of the panel.
        RowLayout {
            id: stepBar
            readonly property bool compact: root.availableWidth < 560
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.stepNames
                delegate: Button {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: !stepBar.compact || index === root.step
                    Layout.preferredWidth: stepBar.compact && index !== root.step ? 40 : -1
                    Layout.minimumWidth: index === root.step ? implicitContentWidth + leftPadding + rightPadding : 0
                    flat: index !== root.step
                    highlighted: index === root.step
                    text: stepBar.compact && index !== root.step ? String(index + 1) : (index + 1) + ". " + modelData
                    ToolTip.visible: stepBar.compact && hovered && index !== root.step
                    ToolTip.text: modelData
                    font.pixelSize: Theme.small
                    onClicked: root.step = index
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            StackLayout {
                width: parent.width
                currentIndex: root.step

                // ---- 1. Personaje
                ColumnLayout {
                    spacing: 6
                    Text {
                        visible: !root.summary.formKey
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.warn
                        text: qsTr("Selecciona un NPC en la lista de la izquierda: vanilla, de otro mod o con replacer.")
                    }
                    ColumnLayout {
                        visible: !!root.summary.formKey
                        Layout.fillWidth: true
                        spacing: 4
                        KeyValue { label: qsTr("Nombre"); value: root.summary.name || root.summary.editorId || "" }
                        KeyValue { label: qsTr("FormKey"); value: root.summary.formKey || ""; mono: true }
                        KeyValue { label: qsTr("Plugin de origen"); value: root.summary.originPlugin || "" }
                        KeyValue {
                            label: qsTr("Plugin ganador")
                            value: (root.summary.winningPlugin || "") + (root.summary.overrideCount > 0 ? qsTr("  (%1 override(s))").arg(root.summary.overrideCount) : "")
                        }
                        KeyValue { label: qsTr("Raza"); value: root.summary.raceEditorId || "" }
                        KeyValue {
                            label: qsTr("Sexo")
                            value: root.summary.sex === "female" ? qsTr("Mujer") : root.summary.sex === "male" ? qsTr("Hombre") : (root.summary.sex || "")
                        }
                        KeyValue {
                            label: qsTr("Rostro")
                            value: root.studio.appearance.analysing ? qsTr("Analizando el FaceGen…") : (root.compat.label || qsTr("Sin diagnóstico"))
                        }
                        Text {
                            visible: !!(root.compat.provenance && root.compat.provenance.conflict)
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.warn
                            font.pixelSize: Theme.small
                            text: "⚠ " + ((root.compat.provenance && root.compat.provenance.conflict) || "")
                        }
                    }
                }

                // ---- 2. Modificación
                ColumnLayout {
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("El mod contiene lo que editaste: el cabello y el rostro en Character Creator, el atuendo en Outfit "
                                   + "Studio. El asistente elige el tipo de mod con el cambio mínimo.")
                    }
                    Text { text: root.plan.summary || ""; color: Theme.text; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Repeater {
                        model: root.plan.changes || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: Theme.small
                            text: "✓ " + modelData.label + (modelData.detail ? " — " + modelData.detail : "")
                        }
                    }
                    Text {
                        visible: (root.compat.blocked || []).length > 0 && !root.studio.appearance.analysing
                        text: qsTr("No disponible para este NPC")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: Theme.small
                    }
                    Repeater {
                        model: root.studio.appearance.analysing ? [] : (root.compat.blocked || [])
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.warn
                            font.pixelSize: Theme.small
                            text: "✕ " + modelData.label + " — " + modelData.reason
                        }
                    }
                }

                // ---- 3. Vista previa
                ColumnLayout {
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("El visor muestra los modelos reales del perfil. Para el rostro, la cabeza que ves es exactamente "
                                   + "el FaceGen que se va a exportar.")
                    }
                    Button {
                        text: root.studio.scene.original ? qsTr("Ver modificado") : qsTr("Comparar con el original")
                        enabled: !!root.summary.formKey
                        onClicked: root.studio.scene.original = !root.studio.scene.original
                    }
                    Text {
                        visible: root.plan.mode === "face-resolution"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("El parche no cambia la cabeza: el visor ya muestra el FaceGen que carga el juego, y el parche hace que el "
                                   + "registro diga lo mismo que ese FaceGen.")
                    }
                    Text {
                        visible: root.studio.appearance.faceGen.kind === "hair-safe"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: root.studio.appearance.faceGen.integrity === "hair-tint-only" ? Theme.ok : Theme.warn
                        font.pixelSize: Theme.small
                        text: root.studio.appearance.faceGen.integrity === "hair-tint-only"
                              ? qsTr("Hair Color Safe Mode: %1 pieza(s) recoloreada(s); el resto del FaceGen ganador es idéntico.")
                                    .arg((root.studio.appearance.faceGen.recoloredShapes || []).length)
                              : qsTr("Hair Color Safe Mode no confirmó el color: revisa el panel de problemas.")
                    }
                }

                // ---- 4. Comprobación
                // A Column (positioner), not a ColumnLayout: a ColumnLayout holding only a Repeater
                // crashed Qt Quick Layouts 6.11 (access violation) when this StackLayout page was shown.
                Item {
                    implicitHeight: checksColumn.implicitHeight
                    Column {
                        id: checksColumn
                        width: parent.width
                        spacing: 4
                        Text {
                            visible: root.dependencies.length > 0
                            width: checksColumn.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.small
                            color: Theme.warn
                            text: qsTr("⚠ El mod necesitará, como mínimo, estos plugins activos (si faltan, Skyrim no lo carga):")
                                  + root.dependencies.map(function (d) {
                                        return "\n     " + d.plugin + (d.provider.length > 0 ? qsTr("  (mod de MO2: %1)").arg(d.provider) : "")
                                    }).join("")
                        }
                        Text {
                            // Only modes B and C write a FaceGen; mode A leaves the head to the winning mod.
                            readonly property var prov: root.compat.provenance || ({})
                            readonly property var competitors: prov.faceGenCompetitors || []
                            readonly property bool writesFaceGen: root.plan.mode === "face-overhaul" || root.plan.mode === "complete-overhaul"
                            visible: writesFaceGen && (competitors.length > 0 || !!(prov.faceGen && prov.faceGen.kind === "BSA"))
                            width: checksColumn.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.small
                            color: competitors.length > 0 ? Theme.warn : Theme.textDim
                            text: competitors.length > 0
                                  ? qsTr("⚠ Prioridad en MO2: instala este mod por encima de %1. También tienen el FaceGen de este NPC; si alguno queda por encima, Skyrim usará su cabeza con este plugin (cara oscura o distinta). El estudio no cambia las prioridades.")
                                        .arg(competitors.map(function (m) { return "«" + m + "»" }).join(", "))
                                  : qsTr("El FaceGen actual está dentro de un BSA (%1): el archivo suelto de este mod siempre lo sustituye, sea cual sea su prioridad.")
                                        .arg(prov.faceGen ? (prov.faceGen.archiveProvider || prov.faceGen.provider || "") : "")
                        }
                        Repeater {
                            model: root.plan.checks || []
                            delegate: Text {
                                required property var modelData
                                width: checksColumn.width
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.small
                                color: root.stateColor(modelData.state)
                                text: root.stateIcon(modelData.state) + "  " + modelData.label
                                      + (modelData.detail.length > 0 ? "\n     " + modelData.detail : "")
                            }
                        }
                    }
                }

                // ---- 5. Crear mod
                ColumnLayout {
                    spacing: 6
                    Label { text: qsTr("Nombre del plugin") }
                    TextField {
                        id: pluginName
                        Layout.fillWidth: true
                        validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9][A-Za-z0-9 _\-\.\(\)\[\]']{0,95}\.esp/ }
                    }
                    Label { text: qsTr("Autor") }
                    TextField { id: author; Layout.fillWidth: true; placeholderText: qsTr("Tu nombre (opcional)") }
                    Label { text: qsTr("Descripción") }
                    TextField { id: description; Layout.fillWidth: true; placeholderText: qsTr("Opcional") }
                    Label { text: qsTr("Carpeta de salida") }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: outputDir; Layout.fillWidth: true }
                        Button { text: qsTr("Elegir…"); onClicked: folderDialog.open() }
                    }
                    CheckBox { id: esl; text: qsTr("ESL (ESP-FE: no ocupa un índice de carga completo)"); checked: true }
                    CheckBox { id: zip; text: qsTr("ZIP instalable con Mod Organizer 2"); checked: true }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: Theme.small
                        text: qsTr("Se escribe en una carpeta temporal, se verifica y solo entonces se mueve al destino. No se instala "
                                   + "en MO2 ni se toca Skyrim/Data.")
                    }
                    Text {
                        visible: !root.plan.canExport && !root.busy
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.warn
                        font.pixelSize: Theme.small
                        text: qsTr("Aún no se puede crear: revisa el paso 4.")
                    }
                    Button {
                        text: root.busy ? qsTr("Creando…") : qsTr("Crear mod")
                        highlighted: true
                        enabled: root.plan.canExport && !root.busy && pluginName.acceptableInput && outputDir.text.trim().length > 0
                        onClicked: root.createMod()
                    }

                    // Result
                    ColumnLayout {
                        visible: root.modResult !== null
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: qsTr("✔ Mod creado y verificado"); color: Theme.ok; font.bold: true }
                        KeyValue { label: qsTr("Plugin"); value: root.modResult ? (root.modResult.pluginPath || "") : ""; mono: true }
                        KeyValue { label: qsTr("ZIP"); value: root.modResult && root.modResult.zipPath ? root.modResult.zipPath : qsTr("(no solicitado)"); mono: true }
                        KeyValue { label: qsTr("Masters"); value: root.modResult ? (root.modResult.masters || []).join(", ") : "" }
                        Text {
                            readonly property var projected: root.modResult ? (root.modResult.projectedControl || null) : null
                            visible: projected !== null && projected.facePlugin !== undefined
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.ok
                            font.pixelSize: Theme.small
                            text: projected ? qsTr("Mapa de control con el parche al final: datos faciales de %1 (mod «%2»), copiados por el parche; ningún desajuste de FaceGen.")
                                                  .arg(projected.facePlugin).arg(projected.faceMod) : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.warn
                            font.pixelSize: Theme.small
                            text: qsTr("Pendiente dentro del juego:") + "\n• "
                                  + (root.modResult && (root.modResult.pendingVerification || []).length > 0
                                     ? root.modResult.pendingVerification.join("\n• ")
                                     : qsTr("instalar el mod en un perfil de prueba de MO2 y comprobarlo en Skyrim."))
                        }
                        Text { text: qsTr("Cambios respecto al registro ganador"); color: Theme.accent; font.bold: true; font.pixelSize: Theme.small }
                        Repeater {
                            model: root.modResult ? (root.modResult.semanticDiff || []) : []
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 1
                                Repeater {
                                    model: modelData.changes
                                    delegate: Text {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAnywhere
                                        color: Theme.textDim
                                        font.pixelSize: Theme.small
                                        text: "• " + modelData.field + " (" + modelData.subrecord + "): " + modelData.original + "  →  " + modelData.exported
                                    }
                                }
                            }
                        }
                        Text { text: qsTr("Archivos y comprobaciones"); color: Theme.accent; font.bold: true; font.pixelSize: Theme.small }
                        Repeater {
                            model: root.modResult ? (root.modResult.files || []) : []
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: "• " + modelData.path + " (" + modelData.bytes + " B)"
                            }
                        }
                        Repeater {
                            model: root.modResult ? (root.modResult.verification || []) : []
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                text: "✓ " + modelData
                            }
                        }
                        Button {
                            text: qsTr("Abrir carpeta")
                            onClicked: root.studio.openFolder(root.modResult.modDirectory)
                        }
                    }
                    ColumnLayout {
                        visible: root.failure.length > 0
                        Layout.fillWidth: true
                        Text { text: "✖ " + root.failure; color: Theme.error; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        IssueList { issues: root.failureIssues; emptyText: "" }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Atrás"); enabled: root.step > 0; onClicked: root.step-- }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Siguiente"); visible: root.step < 4; highlighted: true; onClicked: root.step++ }
            Button { text: qsTr("Cerrar"); flat: true; onClicked: root.close() }
        }
    }
}
