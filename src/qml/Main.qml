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

    // ── palette (self-contained, no design-system dependency) ──
    readonly property color bgPrimary:    "#0e0f12"
    readonly property color bgSecondary:  "#181a1f"
    readonly property color borderColor:  "#2a2d34"
    readonly property color textPrimary:  "#e8e8ea"
    readonly property color textSecondary:"#9aa0aa"
    readonly property color textMuted:    "#5c626c"
    readonly property color accent:       "#ff5a00"
    readonly property color ok:           "#36c26b"
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
                        from: 2; to: 20; stepSize: 1
                        value: root.listenBuffer
                        onPressedChanged: if (!pressed && backend) backend.setBuffer(Math.round(value))
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
                        checked: root.hideCache
                        onToggled: if (backend) backend.setCacheHidden(checked)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                Rectangle {
                    Layout.alignment: Qt.AlignLeft
                    implicitWidth: 110; height: 28; radius: 4
                    color: clearArea.containsMouse ? "#33373f" : root.bgPrimary
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
                    width: list.width; height: 52; radius: 4
                    color: rowArea.containsMouse ? "#22252b" : "transparent"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 10
                        Rectangle { width: 8; height: 8; radius: 4; color: root.ok }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 0
                            Text { text: modelData.name || "(unnamed)"; color: root.textPrimary; font.pixelSize: 13 }
                            Text {
                                text: (modelData.host || "anonymous") + " · " + (modelData.privacy || "")
                                color: root.textSecondary; font.pixelSize: 10
                            }
                        }
                        Text {
                            text: (root.nowPlaying === modelData.name) ? "▶ playing" : "tap to play"
                            color: (root.nowPlaying === modelData.name) ? root.accent : root.textMuted
                            font.pixelSize: 11
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
                anchors.fill: parent; anchors.margins: 10; spacing: 10
                Text { text: "▶"; color: root.accent; font.pixelSize: 14 }
                Text { text: root.nowPlaying; color: root.textPrimary; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                Rectangle {
                    width: 64; height: 26; radius: 4; color: stopArea.containsMouse ? "#33373f" : root.bgPrimary
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
