import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only).
// Binds to its C++ backend (a Qt Remote Objects source) via logos.module("receiver_ui"):
//   PROPs read directly (backend.nodeReady), SIGNALs via Connections, SLOTs called directly.
Item {
    id: root
    anchors.fill: parent

    readonly property var backend: (typeof logos !== "undefined" && logos.module)
                                   ? logos.module("receiver_ui") : null

    // ── palette — matches radio_ui / keeper / stash ──
    readonly property color bgPrimary:    "#171717"
    readonly property color bgSecondary:  "#262626"
    readonly property color bgActive:     "#2E2E2E"   // neutral hover-lift (was warm #332A27 — read as reddish)
    readonly property color borderColor:  "#383838"
    readonly property color textPrimary:  "#FFFFFF"
    readonly property color textSecondary:"#A4A4A4"
    readonly property color textMuted:    "#5D5D5D"
    readonly property color accent:       "#FF5000"
    readonly property color ok:           "#22C55E"
    readonly property string monoFont:    "monospace"

    property bool settingsOpen: false
    property var  events: []

    readonly property string status:      backend ? backend.connectionStatus : "no backend"
    readonly property bool   nodeReady:    backend ? backend.nodeReady    : false
    readonly property bool   discovering:  backend ? backend.discovering  : false
    readonly property string nowPlaying:   backend ? backend.nowPlaying   : ""
    readonly property int    listenBuffer: backend ? backend.listenBuffer : 8
    readonly property bool   hideCache:    backend ? backend.hideCache    : false

    function stations() {
        if (!backend) return []
        try { return JSON.parse(backend.stationsJson) } catch (e) { return [] }
    }

    Connections {
        target: backend
        ignoreUnknownSignals: true
        function onActivity(line) {
            var next = root.events.slice()
            next.unshift("[" + Qt.formatTime(new Date(), "hh:mm:ss") + "] " + line)
            if (next.length > 100) next = next.slice(0, 100)
            root.events = next
        }
    }

    Rectangle { anchors.fill: parent; color: root.bgPrimary }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        // ── Header: title + status pills + cogwheel ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                spacing: 0
                Text { text: "Receiver"; color: root.textPrimary; font.pixelSize: 20; font.bold: true }
                Text { text: "Discover & listen — decentralized radio"; color: root.textSecondary; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }

            // status pill
            Rectangle {
                radius: 12; height: 24; implicitWidth: pillRow.implicitWidth + 20
                color: root.bgSecondary; border.color: root.borderColor; border.width: 1
                RowLayout {
                    id: pillRow
                    anchors.centerIn: parent; spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.discovering ? root.ok : (root.nodeReady ? "#d2a106" : root.textMuted)
                    }
                    Text {
                        color: root.textSecondary; font.pixelSize: 11
                        text: root.discovering ? "Discovering" : (root.nodeReady ? "Node ready" : root.status)
                    }
                }
            }

            // cogwheel
            Rectangle {
                width: 28; height: 28; radius: 6
                color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.settingsOpen ? root.accent : root.borderColor; border.width: 1
                Text { anchors.centerIn: parent; text: "⚙"; font.pixelSize: 15
                       color: root.settingsOpen ? root.accent : root.textSecondary }
                MouseArea {
                    id: gearArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsOpen = !root.settingsOpen
                }
            }
        }

        // ── Settings pane (cogwheel) ──
        Rectangle {
            Layout.fillWidth: true
            visible: root.settingsOpen
            implicitHeight: setCol.implicitHeight + 20
            color: root.bgSecondary; radius: 6; border.color: root.borderColor; border.width: 1

            ColumnLayout {
                id: setCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
                spacing: 10

                // Listener buffer
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Listener buffer"; color: root.textSecondary; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text { text: root.listenBuffer + "s"; color: root.textPrimary; font.pixelSize: 11; font.family: root.monoFont }
                    }
                    Slider {
                        id: bufSlider
                        Layout.fillWidth: true
                        implicitHeight: 18
                        from: 2; to: 20; stepSize: 1
                        value: root.listenBuffer
                        onPressedChanged: if (!pressed && backend) backend.setBuffer(Math.round(value))
                        background: Rectangle {
                            x: bufSlider.leftPadding; y: bufSlider.topPadding + bufSlider.availableHeight / 2 - height / 2
                            width: bufSlider.availableWidth; height: 4; radius: 2
                            color: root.borderColor
                            Rectangle { width: bufSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.accent }
                        }
                        handle: Rectangle {
                            x: bufSlider.leftPadding + bufSlider.visualPosition * (bufSlider.availableWidth - width)
                            y: bufSlider.topPadding + bufSlider.availableHeight / 2 - height / 2
                            implicitWidth: 14; implicitHeight: 14; radius: 7
                            color: bufSlider.pressed ? "#CC4000" : root.accent
                            border.color: root.bgPrimary; border.width: 2
                        }
                    }
                    Text {
                        text: "Seconds behind live — rides out Tor latency so audio doesn't chop."
                        color: root.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                // Hide cache (privacy)
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        Text { text: "Hide cache"; color: root.textPrimary; font.pixelSize: 12 }
                        Text {
                            text: "Suppress + clear on-disk cache of streamed audio (privacy)."
                            color: root.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                    }
                    Switch {
                        id: hideSw
                        padding: 0
                        implicitWidth: 36; implicitHeight: 18
                        Layout.preferredWidth: 36; Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter
                        checked: root.hideCache
                        onToggled: if (backend) backend.setCacheHidden(checked)
                        indicator: Rectangle {
                            anchors.fill: parent; radius: 9
                            color: hideSw.checked ? root.accent : root.bgPrimary
                            border.color: hideSw.checked ? root.accent : root.borderColor; border.width: 1
                            Rectangle {
                                x: hideSw.checked ? parent.width - width - 2 : 2
                                y: 2; width: 14; height: 14; radius: 7; color: "#FFFFFF"
                                Behavior on x { NumberAnimation { duration: 120 } }
                            }
                        }
                        contentItem: Item {}   // label lives in the row text, not on the switch
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                Rectangle {
                    Layout.alignment: Qt.AlignLeft
                    implicitWidth: 110; height: 28; radius: 4
                    color: clearArea.containsMouse ? root.bgActive : root.bgPrimary
                    border.color: root.borderColor; border.width: 1
                    Text { anchors.centerIn: parent; text: "Clear cache now"; color: root.textSecondary; font.pixelSize: 11 }
                    MouseArea {
                        id: clearArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (backend) backend.clearCache()
                    }
                }
            }
        }

        // ── Add a private topic ──
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Rectangle {
                Layout.fillWidth: true; height: 32; radius: 4
                color: root.bgSecondary; border.color: root.borderColor; border.width: 1
                TextField {
                    id: topicField
                    anchors.fill: parent; anchors.margins: 4; background: null
                    color: root.textPrimary; font.pixelSize: 12
                    placeholderText: "+ Add a private topic (/radio-basecamp/1/<id>/json)"
                    placeholderTextColor: root.textMuted
                    onAccepted: { if (backend && text.trim().length) { backend.addTopic(text.trim()); text = "" } }
                }
            }
        }

        // ── Station list ──
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            color: root.bgSecondary; radius: 6; border.color: root.borderColor; border.width: 1

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 6; clip: true; spacing: 4
                model: root.stations()
                delegate: Rectangle {
                    width: list.width; height: 52; radius: 6
                    // subtle neutral base (#1E1E1E ≈ half the contrast vs the panel); hover = the current
                    // darker bg (#171717) — neutral inset, no reddish tint
                    color: rowArea.containsMouse ? root.bgPrimary : "#1E1E1E"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12; anchors.topMargin: 8; anchors.bottomMargin: 8
                        spacing: 10   // gap between the status dot and the station name
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: root.ok; Layout.alignment: Qt.AlignVCenter }
                        ColumnLayout {
                            // NOTE: do NOT set Layout.alignment here — it silently disables Layout.fillWidth,
                            // which collapses the column and lets the RowLayout scatter slack as a big dot↔name gap.
                            Layout.fillWidth: true; spacing: 0
                            Text { text: modelData.name || "(unnamed)"; color: root.textPrimary; font.pixelSize: 13 }
                            Text {
                                text: (modelData.host || "anonymous") + " · " + (modelData.privacy || "")
                                color: root.textSecondary; font.pixelSize: 10
                            }
                        }
                        Text {
                            text: (root.nowPlaying === modelData.name) ? "playing" : "tap to play"
                            color: (root.nowPlaying === modelData.name) ? root.accent : root.textMuted
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                    }
                    MouseArea {
                        id: rowArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (backend) backend.play(modelData.streamUrl, modelData.name)
                    }
                }

                // empty state
                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    color: root.textMuted; font.pixelSize: 12
                    text: root.discovering ? "Listening for stations…\nnone announced yet"
                                           : "Starting discovery…"
                }
            }
        }

        // ── Player bar ──
        Rectangle {
            Layout.fillWidth: true; height: 44; radius: 6
            visible: root.nowPlaying.length > 0
            color: root.bgSecondary; border.color: root.accent; border.width: 1
            RowLayout {
                // leftMargin 14 aligns ▶ with the station-row dot (list margin 6 + row margin 8);
                // rightMargin 18 keeps Stop off the edge and right-aligned with the rows' content.
                anchors { fill: parent; leftMargin: 14; rightMargin: 18; topMargin: 8; bottomMargin: 8 }
                spacing: 10
                Text { text: "▶"; color: root.accent; font.pixelSize: 12; Layout.preferredWidth: 8
                       horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignVCenter }
                Text { text: root.nowPlaying; color: root.textPrimary; font.pixelSize: 13; Layout.fillWidth: true
                       elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                Rectangle {
                    Layout.preferredWidth: 64; Layout.preferredHeight: 26; radius: 4; Layout.alignment: Qt.AlignVCenter
                    color: stopArea.containsMouse ? root.bgActive : root.bgPrimary
                    border.color: root.borderColor; border.width: 1
                    Text { anchors.centerIn: parent; text: "Stop"; color: root.textSecondary; font.pixelSize: 11 }
                    MouseArea { id: stopArea; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (backend) backend.stopPlayback() }
                }
            }
        }

        // ── Activity log ──
        Rectangle {
            Layout.fillWidth: true; height: 96; radius: 6
            color: root.bgSecondary; border.color: root.borderColor; border.width: 1
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 2
                Text { text: "Activity"; color: root.textSecondary; font.pixelSize: 10 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.events
                    delegate: Text { text: modelData; color: root.textMuted; font.pixelSize: 10; font.family: root.monoFont }
                }
            }
        }
    }
}
