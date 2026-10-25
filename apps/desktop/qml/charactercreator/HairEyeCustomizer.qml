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

    readonly property bool isFemale: Boolean((root.studio.npc.summary || {}).female)

    // Palette with 16 curated natural and fantasy Skyrim hair shades
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

    // 12 curated Skyrim iris styles and tones
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
        { name: qsTr("Negro Vacío"), color: "#1A1A1A", query: "black" }
    ]

    // Hair secondary highlights / ombré tones
    readonly property var ombrePresets: [
        { name: qsTr("Sin mechas"), color: "#FFFFFF" },
        { name: qsTr("Rubio Solar"), color: "#F5D77F" },
        { name: qsTr("Cobrizo"), color: "#C86432" },
        { name: qsTr("Oro Rosa"), color: "#E8A598" },
        { name: qsTr("Ceniza Plata"), color: "#C0C0C0" },
        { name: qsTr("Azul Glacial"), color: "#70A0D0" }
    ]

    property color selectedOmbreColor: "#FFFFFF"
    property real ombreIntensity: 0.0
    property real hairGlossiness: 0.35
    property real hairRoughness: 0.70
    property real eyeWetness: 0.85
    property real pupilAperture: 0.50
    property real eyeGlow: 0.0
    property bool syncBrowsWithHair: true

    ColorDialog {
        id: customHairColorDialog
        title: qsTr("Elegir color de cabello personalizado")
        selectedColor: root.studio.appearance.hairColor
        onAccepted: root.studio.appearance.setHairColor(selectedColor)
    }

    // Hair color edits need the analysed FaceGen and a HairTint shader (Hair Color Safe Mode).
    readonly property bool hairColorAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "hair-color")
    readonly property bool headPartsAllowed: !root.studio.appearance.analysing
        && !(root.studio.appearance.faceCompatibility.blocked || []).some(b => b.op === "head-parts")

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        // =========================================================================
        // SUB-NAVIGATION SEGMENTADA (STITCH PARADIGM)
        // =========================================================================
        SegmentedControl {
            Layout.fillWidth: true
            stretch: true
            model: [
                { label: qsTr("Peinado & Cabello"), glyph: "tag", tip: qsTr("Estilos de peinado, tinte principal, mechas y brillo") },
                { label: qsTr("Ojos & Mirada"), glyph: "eye", tip: qsTr("Piezas de ojos, presets de iris y reflejos") },
                { label: qsTr("Cejas & Barba"), glyph: "person", tip: qsTr("Cejas, vello facial y sincronización de color") }
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

            // Tarjeta de Peinado Activo
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
                            text: root.activeHair ? (root.activeHair.plugin || qsTr("Mod")) : qsTr("Vanilla / Raza")
                            tone: root.activeHair ? Theme.ok : Theme.textDim
                        }

                        Button {
                            visible: Boolean(root.activeHair)
                            text: "✕ " + qsTr("Quitar")
                            Accessible.name: qsTr("Quitar peinado asignado")
                            implicitHeight: 22
                            onClicked: root.studio.appearance.removeHeadPart("Hair")
                        }
                    }

                    Text {
                        text: root.activeHair ? (root.activeHair.name || root.activeHair.editorId || root.activeHair.formKey)
                                              : qsTr("Peinado predeterminado de la raza del NPC")
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: Boolean(root.activeHair && root.activeHair.meshPath)
                        text: (root.activeHair && root.activeHair.meshPath) ? String(root.activeHair.meshPath) : ""
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        font.family: Theme.mono
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
            }

            // Chips de filtrado rápido por mods populares de cabello
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Filtro:")
                    color: Theme.textDim
                    font.pixelSize: Theme.micro
                }

                Repeater {
                    model: [
                        { label: qsTr("Todos"), filter: "" },
                        { label: "KS Hairdos", filter: "KS Hairdo" },
                        { label: "HG Hairdos", filter: "HG" },
                        { label: "Vanilla", filter: "Skyrim.esm" },
                        { label: "Apachii", filter: "Apachii" }
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

            // Selector de Color Principal de Cabello
            SectionHeader {
                title: qsTr("Color de Cabello Principal")
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

                Text {
                    text: root.studio.appearance.hairColor.toString().toUpperCase()
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.normal
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Selector libre…")
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
                        Layout.fillWidth: true
                        implicitHeight: 34
                        enabled: root.hairColorAllowed
                        Accessible.name: qsTr("Color de cabello %1").arg(modelData.name)
                        ToolTip.text: modelData.name + " (" + modelData.color + ")"
                        ToolTip.visible: hovered

                        contentItem: RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 16
                                height: 16
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

            // Mechas secundarias / Ombré
            SectionHeader {
                title: qsTr("Mechas Secundarias & Reflejos (Ombré)")
                detail: Math.round(root.ombreIntensity * 100) + "%"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: root.ombrePresets
                    delegate: Button {
                        required property var modelData
                        implicitHeight: 26
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Mecha %1").arg(modelData.name)
                        ToolTip.text: modelData.name
                        ToolTip.visible: hovered

                        contentItem: RowLayout {
                            spacing: 4
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 2
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
                            root.selectedOmbreColor = Qt.color(modelData.color)
                            if (modelData.name === qsTr("Sin mechas")) root.ombreIntensity = 0.0
                            else if (root.ombreIntensity === 0.0) root.ombreIntensity = 0.35
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Intensidad:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                }
                Slider {
                    Accessible.name: qsTr("Intensidad de mechas u ombré")
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: root.ombreIntensity
                    onMoved: root.ombreIntensity = value
                }
                Text {
                    text: Math.round(root.ombreIntensity * 100) + "%"
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            // Propiedades del Material / Shader de Cabello
            SectionHeader {
                title: qsTr("Propiedades del Shader de Cabello")
                detail: qsTr("Especular & Rugosidad")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Brillo:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 70
                }
                Slider {
                    Accessible.name: qsTr("Brillo especular del cabello")
                    Layout.fillWidth: true
                    from: 0.05
                    to: 1.0
                    value: root.hairGlossiness
                    onMoved: root.hairGlossiness = value
                }
                Text {
                    text: root.hairGlossiness.toFixed(2)
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Rugosidad:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 70
                }
                Slider {
                    Accessible.name: qsTr("Rugosidad del material de cabello")
                    Layout.fillWidth: true
                    from: 0.1
                    to: 1.0
                    value: root.hairRoughness
                    onMoved: root.hairRoughness = value
                }
                Text {
                    text: root.hairRoughness.toFixed(2)
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            // Catálogo dedicado de Peinados
            SectionHeader {
                title: qsTr("Catálogo de Estilos de Peinado")
                detail: qsTr("Compatibles con Raza y Sexo")
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

            // Tarjeta de Ojos Activos
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
                            text: "✕ " + qsTr("Quitar")
                            Accessible.name: qsTr("Quitar pieza de ojos asignada")
                            implicitHeight: 22
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

            // Explicación técnica de la mecánica de ojos en Skyrim
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: eyeNoticeRow.implicitHeight + 12
                color: Qt.rgba(Theme.control.r, Theme.control.g, Theme.control.b, 0.4)
                radius: Theme.radius
                border.color: Theme.borderSubtle
                border.width: 1

                RowLayout {
                    id: eyeNoticeRow
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    Text {
                        text: "👁"
                        font.pixelSize: Theme.large
                    }

                    Text {
                        text: qsTr("En Skyrim, el color y textura de los ojos provienen de la pieza HeadPart «Eyes» (Eyes of Beauty, Improved Eyes, etc.). Elige una pieza de la lista o filtra por estilo.")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Presets y Estilos de Iris Populares
            SectionHeader {
                title: qsTr("Estilos & Tonalidades de Iris")
                detail: qsTr("Filtro rápido de catálogo")
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
                        implicitHeight: 32
                        Accessible.name: qsTr("Estilo de iris %1").arg(modelData.name)
                        ToolTip.text: qsTr("Filtrar ojos por estilo: %1").arg(modelData.name)
                        ToolTip.visible: hovered

                        contentItem: RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
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

            // Óptica y Reflejos Oculares
            SectionHeader {
                title: qsTr("Óptica Ocular & Refracción")
                detail: qsTr("Humedad y Pupila")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Humedad:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 70
                }
                Slider {
                    Accessible.name: qsTr("Humedad y brillo corneal")
                    Layout.fillWidth: true
                    from: 0.1
                    to: 1.0
                    value: root.eyeWetness
                    onMoved: root.eyeWetness = value
                }
                Text {
                    text: root.eyeWetness.toFixed(2)
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Pupila:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 70
                }
                Slider {
                    Accessible.name: qsTr("Apertura pupilar")
                    Layout.fillWidth: true
                    from: 0.1
                    to: 1.0
                    value: root.pupilAperture
                    onMoved: root.pupilAperture = value
                }
                Text {
                    text: root.pupilAperture.toFixed(2)
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Resplandor:")
                    color: Theme.textDim
                    font.pixelSize: Theme.small
                    Layout.preferredWidth: 70
                }
                Slider {
                    Accessible.name: qsTr("Resplandor ocular mágico")
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: root.eyeGlow
                    onMoved: root.eyeGlow = value
                }
                Text {
                    text: root.eyeGlow.toFixed(2)
                    color: Theme.text
                    font.pixelSize: Theme.micro
                    Layout.preferredWidth: 32
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

            // Catálogo dedicado de Ojos
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
        // SUBPESTAÑA 2: CEJAS & BARBA
        // =========================================================================
        ColumnLayout {
            visible: root.currentSubTab === 2
            Layout.fillWidth: true
            spacing: 12

            // SECCIÓN: CEJAS
            SectionHeader {
                title: qsTr("Cejas & Definición")
                detail: root.activeBrows ? (root.activeBrows.editorId || root.activeBrows.formKey) : qsTr("Vanilla")
            }

            // Tarjeta de Cejas Activas
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: activeBrowsCol.implicitHeight + 14
                color: Theme.panelAlt
                radius: Theme.radius
                border.color: Theme.borderSubtle
                border.width: 1

                ColumnLayout {
                    id: activeBrowsCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: qsTr("CEJAS ACTUALES")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: Theme.micro
                        }

                        Item { Layout.fillWidth: true }

                        StatusBadge {
                            text: root.activeBrows ? (root.activeBrows.plugin || qsTr("Mod")) : qsTr("Vanilla")
                            tone: root.activeBrows ? Theme.ok : Theme.textDim
                        }

                        Button {
                            visible: Boolean(root.activeBrows)
                            text: "✕ " + qsTr("Quitar")
                            Accessible.name: qsTr("Quitar cejas asignadas")
                            implicitHeight: 22
                            onClicked: root.studio.appearance.removeHeadPart("Eyebrows")
                        }
                    }

                    Text {
                        text: root.activeBrows ? (root.activeBrows.name || root.activeBrows.editorId || root.activeBrows.formKey)
                                               : qsTr("Cejas predeterminadas de la raza")
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.normal
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            CheckBox {
                text: qsTr("Sincronizar color de cejas con el cabello")
                Accessible.name: qsTr("Sincronizar color de cejas con el cabello")
                checked: root.syncBrowsWithHair
                onToggled: root.syncBrowsWithHair = checked
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

            // SECCIÓN: BARBA Y VELLO FACIAL
            SectionHeader {
                title: qsTr("Barba & Vello Facial")
                detail: root.isFemale ? qsTr("No aplicable") : (root.activeBeard ? (root.activeBeard.editorId || root.activeBeard.formKey) : qsTr("Afeitado"))
            }

            // Notificación contextual para NPCs femeninos
            Rectangle {
                visible: root.isFemale
                Layout.fillWidth: true
                implicitHeight: femaleNoticeCol.implicitHeight + 14
                color: Qt.rgba(Theme.control.r, Theme.control.g, Theme.control.b, 0.4)
                radius: Theme.radius
                border.color: Theme.borderSubtle
                border.width: 1

                ColumnLayout {
                    id: femaleNoticeCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        StatusBadge {
                            text: qsTr("NPC Femenino")
                            tone: Theme.textDim
                        }
                        Text {
                            text: qsTr("Vello facial no disponible")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.small
                        }
                    }

                    Text {
                        text: qsTr("Las opciones de barba y vello facial están deshabilitadas automáticamente según las definiciones de raza y género del motor de Skyrim.")
                        color: Theme.textDim
                        font.pixelSize: Theme.micro
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Opciones de Barba para NPCs masculinos
            ColumnLayout {
                visible: !root.isFemale
                Layout.fillWidth: true
                spacing: 8

                // Tarjeta de Barba Activa
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: activeBeardCol.implicitHeight + 14
                    color: Theme.panelAlt
                    radius: Theme.radius
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: activeBeardCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("BARBA ACTUAL")
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.micro
                            }

                            Item { Layout.fillWidth: true }

                            StatusBadge {
                                text: root.activeBeard ? (root.activeBeard.plugin || qsTr("Mod")) : qsTr("Afeitado / Sin barba")
                                tone: root.activeBeard ? Theme.ok : Theme.textDim
                            }

                            Button {
                                visible: Boolean(root.activeBeard)
                                text: "✕ " + qsTr("Quitar")
                                Accessible.name: qsTr("Quitar barba asignada")
                                implicitHeight: 22
                                onClicked: root.studio.appearance.removeHeadPart("FacialHair")
                            }
                        }

                        Text {
                            text: root.activeBeard ? (root.activeBeard.name || root.activeBeard.editorId || root.activeBeard.formKey)
                                                   : qsTr("Sin barba (afeitado limpio)")
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.normal
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

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
        }

        // =========================================================================
        // ADVERTENCIA DE BLOQUEO Y DIAGNÓSTICO FACIAL (BASE ORIGINAL PRESERVADA)
        // =========================================================================
        Text {
            visible: !root.headPartsAllowed
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.small
            text: root.studio.appearance.analysing ? qsTr("Disponible cuando termine el análisis del FaceGen.")
                                                   : qsTr("Bloqueado para esta cabeza: ver «Protección facial».")
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // Diagnóstico colapsable de Protección Facial
        FaceProtectionCard {
            Layout.fillWidth: true
            studio: root.studio
        }
    }
}
