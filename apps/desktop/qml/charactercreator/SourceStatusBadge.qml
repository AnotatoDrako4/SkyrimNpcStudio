import QtQuick
import SkyrimNpcStudio

// Status of one appearance source (sns::appearance::SourceStatus) as a badge: the code the engine reports
// (VALID, UNVERIFIED, CONFLICT, AMBIGUOUS, MISSING) with its Spanish meaning. Colour is never the only signal.
StatusBadge {
    id: root
    required property string status
    objectName: "sourceStatusBadge"
    text: status === "VALID" ? qsTr("VALID · encaja")
        : status === "UNVERIFIED" ? qsTr("UNVERIFIED · sin comprobar")
        : status === "CONFLICT" ? qsTr("CONFLICT · no encaja")
        : status === "AMBIGUOUS" ? qsTr("AMBIGUOUS · decide una persona")
        : status === "MISSING" ? qsTr("MISSING · sin FaceGen")
        : status
    tone: status === "VALID" ? Theme.ok
        : status === "CONFLICT" ? Theme.warn
        : status === "AMBIGUOUS" ? Theme.accent
        : status === "MISSING" ? Theme.error
        : Theme.textDim
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Estado de la fuente: %1").arg(text)
}
