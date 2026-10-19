import QtQuick
import QtQuick.Controls
import SkyrimNpcStudio

// Three columns of a workspace: an explorer on the left, the viewer filling the middle and an inspector
// on the right. Which side panels show and how wide they are comes from a WorkspaceLayout of the
// Workbench, so every workspace keeps its own arrangement across restarts. Widths are stored at 100 %
// interface scale and multiplied by it here; a window too narrow for the stored widths only narrows
// them on screen (the viewer keeps centerMinimum), it never overwrites what the user arranged.
//
//   WorkbenchSplit { layout: Workbench.layout("creator"); leftPane: a; rightPane: b
//                    Rectangle { id: a }  Item { SplitView.fillWidth: true }  Rectangle { id: b } }
SplitView {
    id: split
    required property WorkspaceLayout layout
    property Item leftPane: null
    property Item rightPane: null
    // Design pixels (100 % scale). Panels give way below these only when the window cannot hold them.
    property int leftMinimum: 200
    property int rightMinimum: 280
    property int centerMinimum: 360

    readonly property bool leftShown: leftPane !== null && layout.leftVisible && !layout.maximized
    readonly property bool rightShown: rightPane !== null && layout.rightVisible && !layout.maximized
    property int _appliedLeft: 0       // the widths applySizes gave the panels, to tell a drag apart
    property int _appliedRight: 0
    property string _favor: ""         // the panel dragged last keeps its width; the other gives way first

    orientation: Qt.Horizontal
    handle: SplitHandleBar {}

    // Width the side panels may take together without squeezing the viewer below its minimum.
    function sideRoom() {
        const handles = (leftShown ? 1 : 0) + (rightShown ? 1 : 0)
        return Math.max(0, width - Theme.px(centerMinimum) - handles * Theme.splitHandle)
    }

    function applySizes() {
        if (leftPane) leftPane.visible = leftShown
        if (rightPane) rightPane.visible = rightShown
        if (width <= 0)
            return
        const room = sideRoom()
        let lMin = leftShown ? Theme.px(leftMinimum) : 0
        let rMin = rightShown ? Theme.px(rightMinimum) : 0
        if (lMin + rMin > room) {          // very narrow window or a large scale: minimums share the room
            const k = room / (lMin + rMin)
            lMin = Math.floor(lMin * k)
            rMin = Math.floor(rMin * k)
        }
        let l = leftShown ? Math.max(lMin, Theme.px(layout.leftWidth)) : 0
        let r = rightShown ? Math.max(rMin, Theme.px(layout.rightWidth)) : 0
        let over = l + r - room
        if (over > 0 && _favor !== "") {   // the other panel gives way first, down to its minimum
            if (_favor === "left") { const cut = Math.min(r - rMin, over); r -= cut; over -= cut }
            else { const cut = Math.min(l - lMin, over); l -= cut; over -= cut }
        }
        if (over > 0) {                    // take the rest from each panel in proportion to its slack
            const lSlack = l - lMin, rSlack = r - rMin
            const slack = lSlack + rSlack
            if (slack > 0) {
                const lCut = Math.min(lSlack, Math.ceil(over * lSlack / slack))
                l -= lCut
                r -= Math.min(rSlack, over - lCut)
            }
        }
        if (leftShown) {
            leftPane.SplitView.minimumWidth = lMin
            leftPane.SplitView.maximumWidth = Math.max(lMin, room - rMin)
            leftPane.SplitView.preferredWidth = l
            _appliedLeft = l
        }
        if (rightShown) {
            rightPane.SplitView.minimumWidth = rMin
            rightPane.SplitView.maximumWidth = Math.max(rMin, room - lMin)
            rightPane.SplitView.preferredWidth = r
            _appliedRight = r
        }
    }

    // A drag of a divider is the user's choice: the arrangement on screen (both visible panels, at 100 %
    // scale) is kept for this workspace, so a restart at the same window size shows exactly the same.
    onResizingChanged: {
        if (resizing)
            return
        const leftMoved = leftShown && Math.abs(leftPane.width - _appliedLeft) > 1
        const rightMoved = rightShown && Math.abs(rightPane.width - _appliedRight) > 1
        if (!leftMoved && !rightMoved)
            return
        // The dragged panel keeps its new width and the other gives way if the viewer needs the room
        // (during the drag SplitView may squeeze the viewer); then what is on screen is stored.
        _favor = leftMoved ? "left" : "right"
        if (leftMoved)
            layout.leftWidth = Math.round(leftPane.width / Theme.uiFactor)
        else
            layout.rightWidth = Math.round(rightPane.width / Theme.uiFactor)
        applySizes()
        if (leftShown)
            layout.leftWidth = Math.round(_appliedLeft / Theme.uiFactor)
        if (rightShown)
            layout.rightWidth = Math.round(_appliedRight / Theme.uiFactor)
    }

    onWidthChanged: applySizes()
    onLeftShownChanged: applySizes()
    onRightShownChanged: applySizes()
    onLayoutChanged: applySizes()
    Component.onCompleted: applySizes()

    Connections {
        target: split.layout
        function onLeftWidthChanged() { split.applySizes() }
        function onRightWidthChanged() { split.applySizes() }
    }
    Connections {
        target: Theme
        function onUiFactorChanged() { split.applySizes() }
    }
}
