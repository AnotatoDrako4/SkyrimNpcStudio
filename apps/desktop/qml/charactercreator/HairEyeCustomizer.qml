import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SkyrimNpcStudio

ScrollView {
    id: root
    required property StudioController studio
    contentWidth: availableWidth
    clip: true

    property int currentSubTab: 0

    function getHeadPart(type) {
        const parts = root.studio.appearance.headParts || []
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i].type === type) return parts[i]
        }
        return null
    }

    readonly property var activeHair: getHeadPart("Hair")
    readonly property var activeEyes: getHeadPart("Eyes")
    readonly property var activeBrows: getHeadPart("Eyebrows")
    readonly property var activeBeard: getHeadPart("FacialHair")
    readonly property var activeScar: getHeadPart("Scar")

    readonly property bool isFemale: Boolean(root.studio.appearance.female)

    // Curated Skyrim hair color palette (real RGB written to HCLF / HairTint)
    readonly property var hairPalette: [
        { name: qsTr("Negro Azabache"), color: "#111111" },
        { name: qsTr("Ébano Profundo"), color: "#1F1A17" },
        { name: qsTr("Castaño Oscuro"), color: "#3B2219" },
        { name: qsTr("Castaño Medio"), color: "#5A3825" },
        { name: qsTr("Castaño Caoba"), color: "#6E321E" },
        { name: qsTr("Castaño Claro"), color: "#8B5A2B" },
        { name: qsTr("Rubio Trigo"), color: "#B89758" },
        { name: qsTr("Rubio Ceniza"), color: "#C2B280" },
        { name: qsTr("Rubio Dorado"), color: "#E6C280" },
        { name: qsTr("Rubio Platino"), color: "#F0E6D2" },
        { name: qsTr("Blanco Platino"), color: "#ECECEC" },
        { name: qsTr("Cobrizo Nórdico"), color: "#A53A1E" },
        { name: qsTr("Rojo Fuego"), color: "#C0392B" },
        { name: qsTr("Borgoña / Vino"), color: "#5E1924" },
        { name: qsTr("Gris Ceniza"), color: "#808080" },
        { name: qsTr("Azul Mágico"), color: "#2980B9" }
    ]

    // Curated Skyrim iris styles and tones (filters real Eyes HeadPart records)
    readonly property var eyePresets: [
        { name: qsTr("Azul Nórdico"), color: "#4A90E2", query: "blue" },
        { name: qsTr("Azul Glaciar"), color: "#A8DADC", query: "ice" },
        { name: qsTr("Verde Bosque"), color: "#2D6A4F", query: "green" },
        { name: qsTr("Verde Esmeralda"), color: "#52B788", query: "emerald" },
        { name: qsTr("Miel / Ámbar"), color: "#D4A373", query: "amber" },
        { name: qsTr("Castaño Cálido"), color: "#8C5028", query: "brown" },
        { name: qsTr("Marrón Profundo"), color: "#432818", query: "dark" },
        { name: qsTr("Gris Tormenta"), color: "#6C757D", query: "grey" },
        { name: qsTr("Violeta Dunmer"), color: "#7B2CBF", query: "violet" },
        { name: qsTr("Rojo Vampiro"), color: "#9D0208", query: "vampire" },
        { name: qsTr("Ojo Ciego"), color: "#D8D8D8", query: "blind" },
        { name: qsTr("Todos"), color: "#FFFFFF", query: "" }
    ]

    ColorDialog {
        id: customHairColorDialog
        title: qsTr("Elegir color de cabello personalizado (HCLF / HairTint)")
        selectedColor: root.studio.appearance.hairColor
        onAccepted: root.studio.appearance.setHairColor(selectedColor)
    }

    readonly property bool hairColorAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "hair-color")
    readonly property bool headPartsAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "head-parts")

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        Rectangle {
            visible: (!root.headPartsAllowed || !root.hairColorAllowed)
                && Boolean(root.studio.appearance.faceCompatibility.canUnlockForModding)
            Layout.fillWidth: true
            implicitHeight: unlockHairRow.implicitHeight + 12
            radius: Theme.radius
            color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
            border.color: Theme.warn
            border.width: 1

            RowLayout {
                id: unlockHairRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Text {
                    text: qsTr("La protección del FaceGen actual restringe cambios de piezas o cabello. Puedes desbloquear regenerando desde el registro.")
                    color: Theme.text
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Desbloquear para modear")
                    highlighted: true
                    onClicked: root.studio.appearance.unlockForModding()
                }
            }
        }

        SegmentedControl {
            Layout.fillWidth: true
            stretch: true
            model: [
                { label: qsTr("Peinado & Cabello"), glyph: "tag", tip: qsTr("Estilos de peinado reales en MO2, modo calvo y tinte HairTint") },
                { label: qsTr("Ojos & Mirada"), glyph: "eye", tip: qsTr("Piezas HeadPart de ojos y variantes de iris") },
                { label: qsTr("Cejas, Barba & Cicatrices"), glyph: "person", tip: qsTr("Cejas, vello facial y cicatrices reales del registro") }
            ]
            currentIndex: root.currentSubTab
            onActivated: index => root.currentSubTab = index
            accessibleName: qsTr("Subpestañas de personalización de cabello y ojos")
        }

        // =========================================================================
        // SUBPESTAÑA 0: PEINADO & CABELLO
        // =========================================================================
        ColumnLayout {
            visible: root.currentSubTab === 0
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: activeHairCol.implicitHeight + 14
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle
                border.width: 1

                ColumnLayout {
                    id: activeHairCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: qsTr("PEINADO ACTIVO")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.micro
                        }

                        Item { Layout.fillWidth: true }

                        StatusBadge {
                            text: root.studio.appearance.intentionallyBald
                                  ? qsTr("Calvo intencional")
                                  : (root.activeHair ? (root.activeHair.plugin || qsTr("Mod")) : qsTr("Vanilla / Raza"))
                            tone: root.studio.appearance.intentionallyBald ? Theme.warn : (root.activeHair ? Theme.ok : Theme.textDim)
                        }

                        Button {
                            visible: Boolean(root.activeHair) || root.studio.appearance.intentionallyBald
                            text: "↺ " + qsTr("Restaurar original")
                            Accessible.name: qsTr("Restaurar peinado original del NPC")
                            implicitHeight: 22
                            enabled: root.headPartsAllowed
                            onClicked: root.studio.appearance.removeHeadPart("Hair")
                        }
                    }

                    Text {
                        text: root.studio.appearance.intentionallyBald
                              ? qsTr("NPC configurado sin cabello (Calvo intencionalmente)")
                              : (root.activeHair ? (root.activeHair.name || root.activeHair.editorId || root.activeHair.formKey)
                                                 : qsTr("Peinado predeterminado de la raza del NPC"))
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: Boolean(root.activeHair && root.activeHair.meshPath) && !root.studio.appearance.intentionallyBald
                        text: (root.activeHair && root.activeHair.meshPath)
                              ? (String(root.activeHair.meshPath) + (root.activeHair.effectiveProvider ? " · MO2: " + root.activeHair.effectiveProvider : ""))
                              : ""
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        font.family: Theme.mono
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    CheckBox {
                        text: qsTr("NPC calvo intencionalmente (sin malla de cabello)")
                        checked: root.studio.appearance.intentionallyBald
                        enabled: root.headPartsAllowed
                        onToggled: root.studio.appearance.setIntentionallyBald(checked)
                    }
                }
            }

            // Chips de filtrado rápido por mods instalados
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Filtro rápido:")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                }

                Repeater {
                    model: [
                        { label: qsTr("Todos"), filter: "" },
                        { label: "KS Hairdos", filter: "KS Hairdo" },
                        { label: "Apachii", filter: "Apachii" },
                        { label: "HG Hairdos", filter: "HG" },
                        { label: "SMP", filter: "SMP" },
                        { label: "Vanilla", filter: "Skyrim.esm" }
                    ]
                    delegate: Button {
                        required property var modelData
                        text: modelData.label
                        implicitHeight: 22
                        Accessible.name: qsTr("Filtrar por %1").arg(modelData.label)
                        onClicked: {
                            hairGallery.modFilter = ""
                            hairGallery.setFilterText(modelData.filter)
                        }
                    }
                }
            }

            // Selector de Color Principal de Cabello (HCLF / HairTint real)
            SectionHeader {
                title: qsTr("Color de Cabello (HCLF / Shader HairTint)")
                detail: root.studio.appearance.hairColor.toString().toUpperCase()
            }

            Text {
                visible: !root.hairColorAllowed
                Layout.fillWidth: true
                text: root.studio.appearance.analysing ? qsTr("· analizando el FaceGen…") : qsTr("· bloqueado (ver Protección facial)")
                color: Theme.textDim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }

            RowLayout {
                spacing: 10
                Layout.fillWidth: true

                Rectangle {
                    width: 32
                    height: 32
                    radius: 4
                    color: root.studio.appearance.hairColor
                    border.color: Theme.border
                    border.width: 1
                }

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: root.studio.appearance.hairColor.toString().toUpperCase()
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                    }
                    Text {
                        text: qsTr("Se escribe directamente en el nodo BSValueNode / HairTint del FaceGen y en HCLF.")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Selector RGB…")
                    Accessible.name: qsTr("Abrir selector libre de color de cabello")
                    enabled: root.hairColorAllowed
                    onClicked: customHairColorDialog.open()
                }
            }

            // Cuadrícula de 16 colores representativos de Skyrim
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: 6
                columnSpacing: 6

                Repeater {
                    model: root.hairPalette
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 32
                        enabled: root.hairColorAllowed
                        Accessible.name: qsTr("Color de cabello %1").arg(modelData.name)
                        ToolTip.text: modelData.name + " (" + modelData.color + ")"
                        ToolTip.visible: hovered

                        contentItem: RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 3
                                color: modelData.color
                                border.color: Theme.border
                                border.width: 1
                            }
                            Text {
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.micro
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        onClicked: root.studio.appearance.setHairColor(Qt.color(modelData.color))
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            SectionHeader {
                title: qsTr("Catálogo de Estilos de Peinado Instalados")
                detail: qsTr("Compatibles con Raza y Sexo · Requiere reconstrucción FaceGen")
            }

            HeadPartGallery {
                id: hairGallery
                Layout.fillWidth: true
                studio: root.studio
                allowed: root.headPartsAllowed
                category: "Hair"
                showHeader: false
                showCategorySelector: false
            }
        }

        // =========================================================================
        // SUBPESTAÑA 1: OJOS & MIRADA
        // =========================================================================
        ColumnLayout {
            visible: root.currentSubTab === 1
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: activeEyesCol.implicitHeight + 14
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle
                border.width: 1

                ColumnLayout {
                    id: activeEyesCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: qsTr("PIEZA DE OJOS ACTIVA")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.micro
                        }

                        Item { Layout.fillWidth: true }

                        StatusBadge {
                            text: root.activeEyes ? (root.activeEyes.plugin || qsTr("Mod")) : qsTr("Vanilla / Raza")
                            tone: root.activeEyes ? Theme.ok : Theme.textDim
                        }

                        Button {
                            visible: Boolean(root.activeEyes)
                            text: "↺ " + qsTr("Restaurar")
                            Accessible.name: qsTr("Restaurar pieza de ojos original")
                            implicitHeight: 22
                            enabled: root.headPartsAllowed
                            onClicked: root.studio.appearance.removeHeadPart("Eyes")
                        }
                    }

                    Text {
                        text: root.activeEyes ? (root.activeEyes.name || root.activeEyes.editorId || root.activeEyes.formKey)
                                              : qsTr("Ojos predeterminados de la raza")
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: Boolean(root.activeEyes && root.activeEyes.meshPath)
                        text: (root.activeEyes && root.activeEyes.meshPath) ? String(root.activeEyes.meshPath) : ""
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        font.family: Theme.mono
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
            }

            SectionHeader {
                title: qsTr("Filtro Rápido por Tonalidad de Iris")
                detail: qsTr("Filtra registros HeadPart «Eyes» reales")
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 6
                columnSpacing: 6

                Repeater {
                    model: root.eyePresets
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        Accessible.name: qsTr("Filtrar ojos por %1").arg(modelData.name)

                        contentItem: RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: modelData.color
                                border.color: Theme.border
                                border.width: 1
                            }
                            Text {
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.micro
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        onClicked: {
                            eyesGallery.modFilter = ""
                            eyesGallery.setFilterText(modelData.query)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            SectionHeader {
                title: qsTr("Catálogo de Piezas de Ojos")
                detail: qsTr("Compatibles con Raza y Sexo")
            }

            HeadPartGallery {
                id: eyesGallery
                Layout.fillWidth: true
                studio: root.studio
                allowed: root.headPartsAllowed
                category: "Eyes"
                showHeader: false
                showCategorySelector: false
            }
        }

        // =========================================================================
        // SUBPESTAÑA 2: CEJAS, BARBA & CICATRICES
        // =========================================================================
        ColumnLayout {
            visible: root.currentSubTab === 2
            Layout.fillWidth: true
            spacing: 12

            SectionHeader {
                title: qsTr("Cejas (HeadPart Eyebrows)")
                detail: root.activeBrows ? (root.activeBrows.editorId || root.activeBrows.formKey) : qsTr("Vanilla")
            }

            HeadPartGallery {
                id: browsGallery
                Layout.fillWidth: true
                studio: root.studio
                allowed: root.headPartsAllowed
                category: "Eyebrows"
                showHeader: false
                showCategorySelector: false
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            SectionHeader {
                title: qsTr("Barba & Vello Facial (HeadPart FacialHair)")
                detail: root.isFemale ? qsTr("No aplicable en registro femenino") : (root.activeBeard ? (root.activeBeard.editorId || root.activeBeard.formKey) : qsTr("Afeitado"))
            }

            ColumnLayout {
                visible: !root.isFemale
                Layout.fillWidth: true
                spacing: 8

                HeadPartGallery {
                    id: beardGallery
                    Layout.fillWidth: true
                    studio: root.studio
                    allowed: root.headPartsAllowed
                    category: "FacialHair"
                    showHeader: false
                    showCategorySelector: false
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            SectionHeader {
                title: qsTr("Cicatrices & Marcas (HeadPart Scar)")
                detail: root.activeScar ? (root.activeScar.editorId || root.activeScar.formKey) : qsTr("Sin cicatriz")
            }

            HeadPartGallery {
                id: scarGallery
                Layout.fillWidth: true
                studio: root.studio
                allowed: root.headPartsAllowed
                category: "Scar"
                showHeader: false
                showCategorySelector: false
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        FaceProtectionCard {
            Layout.fillWidth: true
            studio: root.studio
        }
    }
}
