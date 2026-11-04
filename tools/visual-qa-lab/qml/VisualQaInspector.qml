// Standalone QML inspection view for Skyrim NPC Studio — Visual QA Lab (AG03).
// Displays clipping incidents, anatomical seam metrics, morph verification results,
// and before/after visual diff captures loaded from out/visual-qa/<run-id>/report.json.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#14161d"
    implicitWidth: 980
    implicitHeight: 680

    property var reportData: ({})
    property string selectedCaptureBefore: ""
    property string selectedCaptureAfter: ""
    property string selectedDiffImage: ""
    property bool showBodyParts: true
    property bool showGarmentParts: true
    property bool highlightClippingZones: true
    property bool highlightSeamRings: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "Skyrim NPC Studio — Visual QA Lab"
                color: "#f0f2f8"
                font.pixelSize: 20
                font.bold: true
            }

            Rectangle {
                radius: 4
                color: root.reportData.primary_npc_evaluation &&
                       root.reportData.primary_npc_evaluation.overall_verdict === "PASS" ? "#1e6f43" : "#8b2635"
                implicitWidth: verdictLabel.implicitWidth + 16
                implicitHeight: 26
                Text {
                    id: verdictLabel
                    anchors.centerIn: parent
                    color: "#ffffff"
                    font.bold: true
                    text: root.reportData.primary_npc_evaluation
                          ? ("NPC Verdict: " + root.reportData.primary_npc_evaluation.overall_verdict)
                          : "PENDIENTE_SKYRIM"
                }
            }

            Item { Layout.fillWidth: true }

            CheckBox {
                checked: root.showBodyParts
                onToggled: root.showBodyParts = checked
                contentItem: Text { text: "Mostrar cuerpo"; color: "#d5d9e6"; leftPadding: 24 }
            }
            CheckBox {
                checked: root.showGarmentParts
                onToggled: root.showGarmentParts = checked
                contentItem: Text { text: "Mostrar ropa"; color: "#d5d9e6"; leftPadding: 24 }
            }
            CheckBox {
                checked: root.highlightClippingZones
                onToggled: root.highlightClippingZones = checked
                contentItem: Text { text: "Resaltar clipping"; color: "#ff9e64"; leftPadding: 24 }
            }
            CheckBox {
                checked: root.highlightSeamRings
                onToggled: root.highlightSeamRings = checked
                contentItem: Text { text: "Resaltar costuras"; color: "#7dcfff"; leftPadding: 24 }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // Left pane: Captures & Before/After Visual Diff
            Rectangle {
                SplitView.preferredWidth: 480
                color: "#1b1e28"
                radius: 6
                border.color: "#2c3142"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Comparacion Visual Antes / Despues / Diferencias"
                        color: "#c0caf5"
                        font.bold: true
                        font.pixelSize: 14
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#11131a"
                            border.color: "#2c3142"
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                source: root.selectedCaptureBefore
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.margins: 6
                                text: "Estado A (Base)"
                                color: "#9aa5ce"
                                font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#11131a"
                            border.color: "#2c3142"
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                source: root.selectedDiffImage !== "" ? root.selectedDiffImage : root.selectedCaptureAfter
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.margins: 6
                                text: root.selectedDiffImage !== "" ? "Mapa de Diferencias" : "Estado B (Preset)"
                                color: "#ff9e64"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // Right pane: Detected Geometric Issues & Seam/Morph Metrics
            Rectangle {
                SplitView.fillWidth: true
                color: "#1b1e28"
                radius: 6
                border.color: "#2c3142"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Incidencias Geometricas Detectadas (Clipping, Costuras, Morphs, Visibilidad)"
                        color: "#c0caf5"
                        font.bold: true
                        font.pixelSize: 14
                    }

                    ListView {
                        id: issuesList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: (root.reportData.primary_npc_evaluation && root.reportData.primary_npc_evaluation.all_issues)
                               ? root.reportData.primary_npc_evaluation.all_issues : []

                        delegate: Rectangle {
                            width: issuesList.width
                            height: issueCol.implicitHeight + 16
                            radius: 4
                            color: "#24283b"
                            border.color: modelData.severity === "error" ? "#f7768e" : "#e0af68"

                            ColumnLayout {
                                id: issueCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                Text {
                                    text: "[" + modelData.severity.toUpperCase() + " / " + modelData.code + "] — Zona: " + modelData.zone
                                    color: modelData.severity === "error" ? "#f7768e" : "#e0af68"
                                    font.bold: true
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData.message
                                    color: "#c0caf5"
                                    font.pixelSize: 12
                                }
                                Text {
                                    text: "Malla: " + modelData.meshPath + " (" + modelData.shapeName + ") | Proveedor: " + modelData.provider
                                    color: "#7aa2f7"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
