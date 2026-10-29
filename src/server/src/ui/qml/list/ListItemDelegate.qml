pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Vicinae

SelectableDelegate {
    id: root
    height: subtitleBelow ? 50 : 38

    Accessible.role: Accessible.ListItem
    Accessible.name: itemTitle
    Accessible.description: itemSubtitle
    Accessible.selectable: true
    Accessible.selected: root.selected

    required property string itemTitle
    required property string itemSubtitle
    required property string itemIconSource
    required property string itemAlias
    property var itemShortcutTokens: []
    required property bool itemIsActive
    property var itemAccessory: []
    property string itemAccessoryColor: ""
    // Show the subtitle on a second line, under the title, in a smaller font,
    // with a larger icon (the keyword_results settings).
    property bool subtitleBelow: false

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.appearance.rowLeftPadding
        anchors.rightMargin: root.appearance.rowRightPadding
        spacing: 10

        Item {
            visible: root.itemIconSource !== ""
            Layout.preferredWidth: root.subtitleBelow ? 32 : 26
            Layout.preferredHeight: root.subtitleBelow ? 32 : 26
            Layout.alignment: Qt.AlignVCenter

            ViciImage {
                anchors.fill: parent
                source: root.itemIconSource
                safetyMargins: true
            }

            Rectangle {
                visible: root.itemIsActive
                width: 4
                height: 4
                radius: 2
                color: Theme.textMuted
                anchors.top: parent.bottom
                anchors.topMargin: 1
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        Item {
            id: textRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 100
            implicitHeight: textHeight

            // The text is placed by position, centred in the row: the title alone, or
            // the title and the subtitle under it as one block.
            readonly property bool twoLines: root.subtitleBelow && subtitleText.visible
            readonly property real lineGap: 1
            readonly property real textHeight: twoLines ? titleText.implicitHeight + lineGap + subtitleText.implicitHeight : titleText.implicitHeight
            readonly property real textTop: Math.round((height - textHeight) / 2)
            readonly property real spacing: root.appearance.rowTextSpacing
            readonly property real shortcutLeadingSpace: 8
            readonly property real aliasSpace: (aliasBadge.visible ? aliasBadge.width + spacing : 0) + (shortcutBadge.visible ? shortcutBadge.width + spacing + shortcutLeadingSpace : 0)
            readonly property real availableForText: width - aliasSpace
            readonly property real subtitleReserved: subtitleText.visible && !root.subtitleBelow ? Math.min(subtitleText.implicitWidth + spacing, availableForText * 0.5) : 0

            Text {
                id: titleText
                anchors.left: parent.left
                y: textRow.textTop
                width: Math.min(implicitWidth, textRow.availableForText - textRow.subtitleReserved)
                text: root.itemTitle
                color: root.selected ? root.appearance.selectedText : root.hovered ? root.appearance.hoveredText : Theme.foreground
                font.pointSize: Theme.regularFontSize
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                id: subtitleText
                visible: root.itemSubtitle !== ""
                anchors.left: root.subtitleBelow ? parent.left : titleText.right
                anchors.leftMargin: visible && !root.subtitleBelow ? textRow.spacing : 0
                anchors.baseline: root.subtitleBelow ? undefined : titleText.baseline
                y: root.subtitleBelow ? titleText.y + titleText.implicitHeight + textRow.lineGap : 0
                width: root.subtitleBelow ? Math.min(implicitWidth, textRow.availableForText) : Math.min(implicitWidth, Math.max(0, textRow.availableForText - titleText.width - textRow.spacing))
                text: root.itemSubtitle
                color: root.selected ? root.appearance.selectedSecondaryText : root.hovered ? root.appearance.hoveredSecondaryText : Theme.textMuted
                font.pointSize: root.subtitleBelow ? Theme.smallerFontSize : Theme.regularFontSize
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            TextBadge {
                id: aliasBadge
                visible: root.itemAlias !== ""
                anchors.left: subtitleText.visible && !root.subtitleBelow ? subtitleText.right : titleText.right
                anchors.leftMargin: visible ? textRow.spacing : 0
                anchors.verticalCenter: root.subtitleBelow ? titleText.verticalCenter : parent.verticalCenter
                text: root.itemAlias
            }

            ShortcutBadge {
                id: shortcutBadge
                visible: root.itemShortcutTokens.length > 0
                anchors.left: aliasBadge.visible ? aliasBadge.right : (subtitleText.visible && !root.subtitleBelow ? subtitleText.right : titleText.right)
                anchors.leftMargin: visible ? textRow.spacing + textRow.shortcutLeadingSpace : 0
                anchors.verticalCenter: root.subtitleBelow ? titleText.verticalCenter : parent.verticalCenter
                tokens: root.itemShortcutTokens
            }
        }

        ListAccessoryRow {
            accessories: {
                if (root.itemAccessory instanceof Array)
                    return root.itemAccessory;
                if (typeof root.itemAccessory === "string" && root.itemAccessory !== "")
                    return [
                        {
                            text: root.itemAccessory,
                            color: root.itemAccessoryColor,
                            fill: false
                        }
                    ];
                return [];
            }
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            clip: true
            opacity: root.quickAccessActive ? 0 : 1

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
