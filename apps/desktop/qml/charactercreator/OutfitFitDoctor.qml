import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SkyrimNpcStudio

// Outfit Fit Doctor — Subsistema de diagnóstico, compatibilidad y reparación de vestuario
Rectangle {
    id: root
    required property StudioController studio
    property OutfitFitDoctorViewModel doctor: null

    Component.onCompleted: {
        if (!doctor) {
            doctor = doctorComponent.createObject(root)
            doctor.init(studio)
        }
    }

    Component {
        id: doctorComponent
        OutfitFitDoctorViewModel {}
    }

    color: Theme.bg
    radius: Theme.radius

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "🩺 Outfit Fit Doctor"
                color: Theme.text
                font.pixelSize: Theme.large
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            StatusBadge {
                text: doctor?.analyzing ? qsTr("ANALIZANDO...") :
                      (doctor?.incompatiblePiecesCount ?? 0) === 0 ? qsTr("COMPATIBLE") : qsTr("%1 INCOMPATIBILIDADES").arg(doctor.incompatiblePiecesCount)
                tone: doctor?.analyzing ? Theme.accent :
                      (doctor?.incompatiblePiecesCount ?? 0) === 0 ? Theme.ok : Theme.error
            }

            Button {
                text: qsTr("🔄 Re-analizar")
                flat: true
                enabled: !(doctor?.analyzing ?? false)
                onClicked: doctor?.runDiagnosis()
            }
        }

        // A/B Comparison and Mode Switcher
        Rectangle {
            Layout.fillWidth: true
            height: Theme.px(42)
            color: Theme.panelAlt
            radius: Theme.radiusSmall
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6

                Text {
                    text: qsTr("Modo de Comparación:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    font.bold: true
                    Layout.leftMargin: 6
                }

                Button {
                    text: qsTr("A — Estado Actual (MO2)")
                    highlighted: (doctor?.activeComparisonMode ?? "A") === "A"
                    onClicked: doctor?.setComparisonMode("A")
                }

                Button {
                    text: qsTr("B — Borrador Corregido")
                    highlighted: (doctor?.activeComparisonMode ?? "A") === "B"
                    enabled: (doctor?.isDraftMode ?? false)
                    onClicked: doctor?.setComparisonMode("B")
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("↶ Deshacer Reparación")
                    enabled: doctor?.canUndoRepair ?? false
                    onClicked: doctor?.undoLastRepair()
                }

                Button {
                    text: qsTr("Restaurar Todo")
                    enabled: doctor?.isDraftMode ?? false
                    onClicked: doctor?.revertAllRepairs()
                }
            }
        }

        // 3D Diagnostics Toolbar (Camera views & Wireframe)
        Rectangle {
            Layout.fillWidth: true
            height: Theme.px(38)
            color: Theme.panel
            radius: Theme.radiusSmall
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                Text {
                    text: qsTr("Cámara 3D:")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                    Layout.leftMargin: 4
                }

                Button {
                    text: qsTr("Frente")
                    onClicked: doctor?.setViewportCameraPreset("front")
                }
                Button {
                    text: qsTr("Lateral")
                    onClicked: doctor?.setViewportCameraPreset("side")
                }
                Button {
                    text: qsTr("Dorsal")
                    onClicked: doctor?.setViewportCameraPreset("back")
                }

                Rectangle { width: 1; height: 18; color: Theme.border }

                Text {
                    text: qsTr("Zoom Anatómico:")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                }

                Button {
                    text: qsTr("Cuello")
                    onClicked: doctor?.setViewportCameraPreset("neck")
                }
                Button {
                    text: qsTr("Torso")
                    onClicked: doctor?.setViewportCameraPreset("torso")
                }
                Button {
                    text: qsTr("Cintura")
                    onClicked: doctor?.setViewportCameraPreset("waist")
                }
                Button {
                    text: qsTr("Muñecas")
                    onClicked: doctor?.setViewportCameraPreset("wrists")
                }
                Button {
                    text: qsTr("Tobillos")
                    onClicked: doctor?.setViewportCameraPreset("ankles")
                }

                Item { Layout.fillWidth: true }

                CheckBox {
                    text: qsTr("Estructura Alámbrica")
                    checked: doctor?.wireframeEnabled ?? false
                    onToggled: doctor?.toggleWireframe(checked)
                }
            }
        }

        // Main Scroll Area with Diagnosis Data
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 8

                // Body Baseline Card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: bodyCol.implicitHeight + 16
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: bodyCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("Cuerpo Base Efectivo del NPC")
                                color: Theme.accent
                                font.pixelSize: Theme.normal
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            StatusBadge {
                                text: doctor?.bodyFamily ?? "Unknown"
                                tone: (doctor?.bodyFamily ?? "Unknown") !== "Unknown" ? Theme.ok : Theme.warn
                            }
                        }

                        Text {
                            text: qsTr("<b>NPC:</b> %1 (%2) · <b>Raza:</b> %3")
                                      .arg(doctor?.npcName ?? "Desconocido")
                                      .arg(doctor?.npcFormKey ?? "")
                                      .arg(doctor?.report?.body?.race ?? "")
                            color: Theme.text
                            font.pixelSize: Theme.small
                        }

                        Text {
                            text: qsTr("<b>Malla de Piel:</b> %1").arg(doctor?.bodyMeshPath ?? "")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                        }

                        Text {
                            text: qsTr("<b>Proveedor MO2:</b> %1").arg(doctor?.bodyProvider ?? "")
                            color: Theme.textDim
                            font.pixelSize: Theme.small
                        }

                        RowLayout {
                            spacing: 12
                            Text {
                                text: qsTr("<b>Morphs TRI:</b> %1").arg((doctor?.bodyHasTri ?? false) ? "✅ Disponibles" : "❌ No detectados")
                                color: (doctor?.bodyHasTri ?? false) ? Theme.ok : Theme.warn
                                font.pixelSize: Theme.small
                            }
                            Text {
                                text: qsTr("<b>Confianza:</b> %1").arg(doctor?.bodyConfidence ?? "")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                            }
                        }
                    }
                }

                // Section: Equipped Pieces Diagnosis
                Text {
                    text: qsTr("Diagnóstico Detallado de Prendas (%1 piezas)").arg(doctor?.pieces?.length ?? 0)
                    color: Theme.text
                    font.pixelSize: Theme.normal
                    font.bold: true
                    Layout.topMargin: 4
                }

                Repeater {
                    model: doctor?.pieces ?? []
                    delegate: Rectangle {
                        id: pieceCard
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitHeight: pieceCol.implicitHeight + 16
                        color: Theme.panel
                        radius: Theme.radius
                        border.color: !modelData.topologyCompatible ? Theme.error
                                      : (modelData.issues.length > 0 ? Theme.warn : Theme.border)
                        border.width: 1

                        ColumnLayout {
                            id: pieceCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            // Header of Piece
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: modelData.armorName.length > 0 ? modelData.armorName : modelData.armorFormKey
                                    color: Theme.text
                                    font.pixelSize: Theme.normal
                                    font.bold: true
                                }

                                Text {
                                    text: "[" + modelData.slotDescription + "]"
                                    color: Theme.textDim
                                    font.pixelSize: Theme.small
                                }

                                Item { Layout.fillWidth: true }

                                StatusBadge {
                                    text: modelData.detectedFamily
                                    tone: modelData.topologyCompatible ? Theme.ok : Theme.error
                                }

                                Button {
                                    text: qsTr("👁 Aislar")
                                    flat: true
                                    onClicked: doctor?.isolatePieceInViewport(index, true)
                                }
                            }

                            // Mesh & Provider
                            Text {
                                text: qsTr("<b>Malla NIF:</b> %1").arg(modelData.effectiveNifPath)
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                                elide: Text.ElideLeft
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("<b>Proveedor MO2:</b> %1 %2")
                                          .arg(modelData.winningProvider)
                                          .arg(modelData.isOverwrite ? "(Sobrescritura)" : "")
                                color: Theme.textDim
                                font.pixelSize: Theme.small
                            }

                            // Geometric Info
                            RowLayout {
                                spacing: 12
                                Text {
                                    text: qsTr("Vértices: %1 · Triángulos: %2").arg(modelData.geometry.vertexCount).arg(modelData.geometry.triangleCount)
                                    color: Theme.textDim
                                    font.pixelSize: Theme.micro
                                }
                                Text {
                                    text: qsTr("Par Peso _0/_1: %1").arg(modelData.weightVariantsValid ? "✅" : "⚠️ Incompleto")
                                    color: modelData.weightVariantsValid ? Theme.ok : Theme.warn
                                    font.pixelSize: Theme.micro
                                }
                                Text {
                                    text: qsTr("Morphs BodySlide: %1").arg(modelData.morphsAvailable ? "✅" : "❌ Estática")
                                    color: modelData.morphsAvailable ? Theme.ok : Theme.textDim
                                    font.pixelSize: Theme.micro
                                }
                            }

                            // Issues list
                            Repeater {
                                model: modelData.issues
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: issueInnerCol.implicitHeight + 8
                                    color: Theme.panelAlt
                                    radius: Theme.radiusSmall
                                    border.color: Theme.severityColor(modelData.severity)
                                    border.width: 1

                                    ColumnLayout {
                                        id: issueInnerCol
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 2

                                        RowLayout {
                                            spacing: 4
                                            Text {
                                                text: modelData.severity === "error" ? "⛔" : (modelData.severity === "warning" ? "⚠️" : "ℹ️")
                                                font.pixelSize: Theme.small
                                            }
                                            Text {
                                                text: "<b>" + modelData.code + "</b>"
                                                color: Theme.severityColor(modelData.severity)
                                                font.pixelSize: Theme.small
                                            }
                                        }

                                        Text {
                                            text: modelData.evidence
                                            color: Theme.text
                                            font.pixelSize: Theme.small
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            visible: Boolean(modelData.possibleFix && modelData.possibleFix.length > 0)
                                            text: "💡 <i>" + modelData.possibleFix + "</i>"
                                            color: Theme.accent
                                            font.pixelSize: Theme.micro
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            // Compatible Alternatives found in MO2
                            ColumnLayout {
                                visible: modelData.compatibleAlternatives.length > 0
                                Layout.fillWidth: true
                                spacing: 4
                                Layout.topMargin: 4

                                Text {
                                    text: qsTr("Alternativas Compatibles Instaladas en MO2:")
                                    color: Theme.accent
                                    font.pixelSize: Theme.small
                                    font.bold: true
                                }

                                Repeater {
                                    model: modelData.compatibleAlternatives
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index

                                        Layout.fillWidth: true
                                        implicitHeight: altRow.implicitHeight + 8
                                        color: Theme.panelAlt
                                        radius: Theme.radiusSmall
                                        border.color: Theme.accent
                                        border.width: 1

                                        RowLayout {
                                            id: altRow
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 6

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Text {
                                                    text: modelData.description
                                                    color: Theme.text
                                                    font.pixelSize: Theme.small
                                                    font.bold: true
                                                    wrapMode: Text.Wrap
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: modelData.dataPath
                                                    color: Theme.textDim
                                                    font.pixelSize: Theme.micro
                                                    elide: Text.ElideLeft
                                                    Layout.fillWidth: true
                                                }
                                            }

                                            Button {
                                                text: qsTr("Aplicar Corrección")
                                                highlighted: true
                                                onClicked: doctor?.applyAlternativeRepair(pieceCard.index, index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Export Actions Footer
        Rectangle {
            Layout.fillWidth: true
            height: Theme.px(48)
            color: Theme.panelAlt
            radius: Theme.radiusSmall
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8

                Text {
                    text: (doctor?.isDraftMode ?? false) ? qsTr("Borrador con %1 reparaciones pendientes.").arg(doctor.proposedRepairs.length)
                                                         : qsTr("Sin cambios pendientes en el borrador.")
                    color: (doctor?.isDraftMode ?? false) ? Theme.accent : Theme.textDim
                    font.pixelSize: Theme.small
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("💾 Exportar Mod de Reparación MO2")
                    highlighted: (doctor?.isDraftMode ?? false)
                    enabled: (doctor?.isDraftMode ?? false)
                    onClicked: {
                        const defaultDir = studio.outfit.defaultExportDirectory
                        // Named after the diagnosed NPC (letters, digits and "_" only)
                        const npc = String(doctor.npcName || "NPC").replace(/[^A-Za-z0-9_]+/g, "_").replace(/^_+|_+$/g, "")
                        doctor.exportDoctorMod(defaultDir, "OutfitFitDoctor_" + (npc.length ? npc : "NPC") + "_Fix")
                    }
                }
            }
        }
    }
}
