import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Logos.Theme      // logos-design-system (native on RC3+ Basecamp) — skill: logos-design-system-adoption
import Logos.Controls   // LogosText / LogosButton / LogosBadge / LogosSlider / LogosSwitch / LogosTextField

// Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only).
//
// ⚠️ MAC WORKAROUND BUILD (branch feat/mac-core-relay). On macOS the ui-host does NOT
// receive cross-module delivery onEvent events (CFRunLoop socket-notifier bug — QTBUG-39488 /
// cpp-sdk #68/#79; see receiver-basecamp#4). So discovery+playback live in the `receiver_relay`
// CORE module (runs in logos_host, where events DO dispatch), and this pure-QML view drives it
// over request/reply via logos.callModule (which works on mac — only the EVENT path is broken).
// When the platform fix ships in a Basecamp AppImage, revert to the direct ui_qml backend on main.
Item {
    id: root
    anchors.fill: parent

    // ── backend shim: same surface the QML below expects (stationsJson, nodeReady, play()…),
    //    but every read/call is a request/reply into the receiver_relay core module. ──
    QtObject {
        id: backend
        property string connectionStatus: "starting"
        property bool   nodeReady:   false
        property bool   discovering: false
        property string peerId:      ""     // delivery node identity — surfaced in the header (cf. delivery-demo)
        property string nowPlaying:  ""
        property int    listenBuffer: 20
        property bool   hideCache:   false
        property string stationsJson: "[]"

        function callRaw(method, args) {
            if (typeof logos === "undefined" || !logos.callModule) return ""
            try { return logos.callModule("receiver_relay", method, args || []) }
            catch (e) { return "" }
        }
        function callParse(method, args) {
            try { var t = JSON.parse(callRaw(method, args)); return (typeof t === "string") ? JSON.parse(t) : t }
            catch (e) { return null }
        }
        // SLOTs the UI calls — forwarded to the relay (playback + topics live there too)
        function setBuffer(n)      { callRaw("setListenBuffer", [n]); listenBuffer = n }
        function setCacheHidden(b) { hideCache = b }   // listen-side privacy flag; relay caches nothing extra
        function clearCache()      { /* relay streams via Tor → ffplay, no on-disk cache to clear */ }
        function addTopic(t)       { callRaw("addTopic", [t]) }
        function play(url, name)   { callRaw("play", [url, name]); nowPlaying = name || "" }
        function stopPlayback()    { callRaw("stop", []); nowPlaying = "" }
    }

    // kick the relay alive (first callModule loads the on-demand core module → its initLogos
    // auto-starts discovery) and poll it for live state.
    Component.onCompleted: backend.callRaw("startDiscovery", [])
    Timer {
        interval: 2000; repeat: true; running: true; triggeredOnStart: true
        onTriggered: {
            var ds = backend.callParse("getDeliveryStatus", [])
            if (ds && ds.ok) {
                backend.connectionStatus = ds.state || "offline"
                // granular delivery states: "offline"|"starting"|"Connected"|"PartiallyConnected"|"Disconnected"
                backend.nodeReady   = (ds.state === "Connected" || ds.state === "PartiallyConnected")
                backend.discovering = (ds.state === "Connected" || ds.state === "PartiallyConnected")
                backend.peerId      = ds.peerId || ""
            }
            var st = backend.callParse("getStations", [])
            if (st && st.ok) backend.stationsJson = JSON.stringify(st.stations || [])
            var ps = backend.callParse("getPlayerStatus", [])
            if (ps && ps.ok) backend.nowPlaying = (ps.state === "playing") ? (ps.station || "") : ""
        }
    }

    // ── palette aliases — semantic Theme tokens, no hardcoded hex (skill: logos-design-system-adoption) ──
    readonly property color bgPrimary:    Theme.palette.background
    readonly property color bgSecondary:  Theme.palette.backgroundSecondary
    readonly property color bgActive:     Theme.palette.surface
    readonly property color borderColor:  Theme.palette.borderHairline
    readonly property color textPrimary:  Theme.palette.text
    readonly property color textSecondary:Theme.palette.textSecondary
    readonly property color textMuted:    Theme.palette.textMuted
    readonly property color accent:       Theme.palette.primary
    readonly property color ok:           Theme.palette.success
    readonly property color standby:      Theme.palette.warning          // node-ready-not-yet-discovering amber
    readonly property color rowBase:      Theme.palette.surfaceRecessed  // recessed row inset inside the panel
    // Monospace family for code-like values (topics, the buffer read-out, the activity trace). The
    // design system ships no mono token, so keep the generic family here — Qt maps it to the platform font.
    readonly property string monoFont:    "monospace"

    property bool settingsOpen: false
    property var  events: []

    // #9 caching-on-play: idle | caching | playing — Caching shows a countdown over the buffer secs
    property string playPhase: "idle"
    property int    cacheLeft:  0
    readonly property color cachingYellow: Theme.palette.warning

    readonly property string status:      backend ? backend.connectionStatus : "no backend"
    // delivery_module node state, verbatim from the relay: "offline" | "ready" | "connected".
    readonly property string deliveryState: backend ? backend.connectionStatus : "offline"
    readonly property string peerId:      backend ? backend.peerId       : ""
    readonly property bool   nodeReady:    backend ? backend.nodeReady    : false
    readonly property bool   discovering:  backend ? backend.discovering  : false
    readonly property string nowPlaying:   backend ? backend.nowPlaying   : ""
    readonly property int    listenBuffer: backend ? backend.listenBuffer : 20
    readonly property bool   hideCache:    backend ? backend.hideCache    : false

    function stations() {
        if (!backend) return []
        try { return JSON.parse(backend.stationsJson) } catch (e) { return [] }
    }

    // #9 — enter the Caching phase (countdown over the listener buffer), then flip to Playing.
    function startPlay(url, name) {
        if (!backend) return
        backend.play(url, name)
        root.playPhase = "caching"
        root.cacheLeft = Math.max(1, root.listenBuffer)
        cacheTimer.restart()
    }
    function stopPlay() {
        if (backend) backend.stopPlayback()
        root.playPhase = "idle"; root.cacheLeft = 0; cacheTimer.stop()
    }

    Timer {
        id: cacheTimer; interval: 1000; repeat: true
        onTriggered: {
            if (root.cacheLeft > 0) root.cacheLeft--
            if (root.cacheLeft <= 0) { root.playPhase = "playing"; cacheTimer.stop() }
        }
    }

    Connections {
        target: backend
        ignoreUnknownSignals: true
        // external stop / relay reports stopped → leave the caching/playing phase
        function onNowPlayingChanged() {
            if (backend && backend.nowPlaying.length === 0 && root.playPhase !== "idle") {
                root.playPhase = "idle"; root.cacheLeft = 0; cacheTimer.stop()
            }
        }
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
        anchors.margins: Theme.spacing.medium
        spacing: Theme.spacing.medium

        // ── Header: title + status badge + cogwheel ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small

            ColumnLayout {
                spacing: 0
                LogosText {
                    text: "Receiver"
                    font.pixelSize: Theme.typography.panelTitleText
                    font.weight: Theme.typography.weightBold
                }
                LogosText {
                    text: "Discover & listen — decentralized radio"
                    color: root.textSecondary
                    font.pixelSize: Theme.typography.secondaryText
                }
                // delivery node identity — surfaced like delivery-demo's "Peer ID:" row (mono, elided).
                LogosText {
                    visible: root.peerId.length > 0
                    text: "node " + root.peerId
                    color: root.textMuted; font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 280; Layout.fillWidth: false
                }
            }
            Item { Layout.fillWidth: true }

            // status badge — same design + logic as delivery-demo's LogosBadge, now on the delivery node's
            // real, live connection state: Connected→success, PartiallyConnected→warning, Disconnected→error,
            // and neutral textSecondary while starting / no node. (LogosBadge renders AllUppercase, so the
            // source stays lowercase like delivery-demo.)
            LogosBadge {
                Layout.alignment: Qt.AlignVCenter
                text:  root.deliveryState === "Connected"         ? "discovering"
                     : root.deliveryState === "PartiallyConnected" ? "partial peers"
                     : root.deliveryState === "Disconnected"       ? "disconnected"
                     : root.deliveryState === "starting"           ? "connecting…"
                     :                                               "no node"
                color: root.deliveryState === "Connected"         ? root.ok
                     : root.deliveryState === "PartiallyConnected" ? root.standby
                     : root.deliveryState === "Disconnected"       ? Theme.palette.error
                     : root.deliveryState === "starting"           ? root.standby
                     :                                               root.textSecondary
            }

            // cogwheel — no gear icon asset ships with the module, so a tokenized custom toggle
            // (LogosIconButton needs an iconSource url). Active state borders in the accent colour.
            Rectangle {
                width: 28; height: 28; radius: Theme.spacing.radiusSmall
                color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.settingsOpen ? root.accent : root.borderColor; border.width: 1
                LogosText {
                    anchors.centerIn: parent; text: "⚙"
                    font.pixelSize: Theme.typography.primaryText
                    color: root.settingsOpen ? root.accent : root.textSecondary
                }
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
            implicitHeight: setCol.implicitHeight + Theme.spacing.large
            color: root.bgSecondary; radius: Theme.spacing.radiusMedium
            border.color: root.borderColor; border.width: 1

            ColumnLayout {
                id: setCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: Theme.spacing.small }
                spacing: Theme.spacing.small

                // Listener buffer
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.tiny
                    RowLayout {
                        Layout.fillWidth: true
                        LogosText { text: "Listener buffer"; color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText }
                        Item { Layout.fillWidth: true }
                        LogosText { text: root.listenBuffer + "s"; font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont }
                    }
                    LogosSlider {
                        id: bufSlider
                        Layout.fillWidth: true
                        from: 2; to: 60; stepSize: 1
                        value: root.listenBuffer
                        onPressedChanged: if (!pressed && backend) backend.setBuffer(Math.round(value))
                    }
                    LogosText {
                        text: "Seconds behind live — rides out Tor latency so audio doesn't chop."
                        color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                // Hide cache (privacy)
                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.small
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        LogosText { text: "Hide cache"; font.pixelSize: Theme.typography.primaryText }
                        LogosText {
                            text: "Suppress + clear on-disk cache of streamed audio (privacy)."
                            color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                    }
                    LogosSwitch {
                        id: hideSw
                        Layout.alignment: Qt.AlignVCenter
                        checked: root.hideCache
                        onToggled: if (backend) backend.setCacheHidden(checked)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                LogosButton {
                    Layout.alignment: Qt.AlignLeft
                    text: "Clear cache now"
                    implicitWidth: 140; implicitHeight: 32
                    onClicked: if (backend) backend.clearCache()
                }
            }
        }

        // ── Add a private topic ──
        LogosTextField {
            id: topicField
            Layout.fillWidth: true
            placeholderText: "+ Add a private topic (/radio-basecamp/1/<id>/json)"
        }
        Connections {
            target: topicField.textInput
            function onAccepted() {
                if (backend && topicField.text.trim().length) { backend.addTopic(topicField.text.trim()); topicField.text = "" }
            }
        }

        // ── Station list ──
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            color: root.bgSecondary; radius: Theme.spacing.radiusMedium
            border.color: root.borderColor; border.width: 1

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: Theme.spacing.tiny; clip: true; spacing: Theme.spacing.tiny
                model: root.stations()
                delegate: Rectangle {
                    width: list.width; height: 52; radius: Theme.spacing.radiusMedium
                    // recessed row inset (surfaceRecessed, subtle vs the panel); hover lifts to the page bg
                    color: rowArea.containsMouse ? root.bgPrimary : root.rowBase
                    // anchor-based row — deterministic positions, no RowLayout slack distribution
                    Rectangle {                 // status dot, far left
                        id: dot
                        anchors.left: parent.left; anchors.leftMargin: Theme.spacing.medium
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4; color: root.ok
                    }
                    // #19: right-side control — a perfect-round ▶ play button (idle) or the
                    // status label (playing/caching). Circle fills accent on hover.
                    Item {
                        id: statusText
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacing.medium
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property bool active: (root.nowPlaying === modelData.name)
                        width: active ? lbl.implicitWidth : 26
                        height: 26
                        Rectangle {                     // circular play button (idle)
                            visible: !statusText.active
                            anchors.centerIn: parent; width: 26; height: 26; radius: 13
                            color: rowArea.containsMouse ? root.accent : "transparent"
                            border.width: 1; border.color: rowArea.containsMouse ? root.accent : root.borderColor
                            LogosText {
                                anchors.centerIn: parent; anchors.horizontalCenterOffset: 1  // optical: nudge ▶ right
                                text: "▶"; font.pixelSize: Theme.typography.secondaryText
                                color: rowArea.containsMouse ? root.bgPrimary : root.textMuted
                            }
                        }
                        LogosText {                     // status label (playing / caching)
                            id: lbl
                            visible: statusText.active
                            anchors.centerIn: parent
                            text: root.playPhase === "caching" ? "caching…" : "playing"
                            color: root.playPhase === "caching" ? root.cachingYellow : root.accent
                            font.pixelSize: Theme.typography.secondaryText
                        }
                    }
                    Column {                     // name + host, exactly small-gap right of the dot
                        anchors.left: dot.right; anchors.leftMargin: Theme.spacing.small
                        anchors.right: statusText.left; anchors.rightMargin: Theme.spacing.small
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        LogosText { text: modelData.name || "(unnamed)"; font.pixelSize: Theme.typography.primaryText; width: parent.width; elide: Text.ElideRight }
                        LogosText { text: (modelData.host || "anonymous") + " · " + (modelData.privacy || "")
                               color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText; width: parent.width; elide: Text.ElideRight }
                    }
                    MouseArea {
                        id: rowArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.startPlay(modelData.streamUrl, modelData.name)
                    }
                }

                // empty state
                LogosText {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                    text: root.discovering ? "Listening for stations…\nnone announced yet"
                                           : "Starting discovery…"
                }
            }
        }

        // ── Player bar (#9: breathing-yellow Caching… countdown → orange Playing) ──
        Rectangle {
            id: playerBar
            Layout.fillWidth: true; height: 44; radius: Theme.spacing.radiusMedium; clip: true
            visible: root.nowPlaying.length > 0
            readonly property bool caching: root.playPhase === "caching"
            color: root.bgSecondary; border.width: 1
            border.color: playerBar.caching ? root.cachingYellow : root.accent

            // #19: caching progress fill — transparent yellow, grows left→right as the cache countdown completes
            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                visible: playerBar.caching
                width: playerBar.caching
                       ? parent.width * Math.max(0, Math.min(1, (root.listenBuffer - root.cacheLeft) / Math.max(1, root.listenBuffer)))
                       : 0
                color: Qt.rgba(root.cachingYellow.r, root.cachingYellow.g, root.cachingYellow.b, 0.18)
                Behavior on width { NumberAnimation { duration: 900; easing.type: Easing.Linear } }
            }

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.spacing.medium; rightMargin: Theme.spacing.medium
                          topMargin: Theme.spacing.small; bottomMargin: Theme.spacing.small }
                spacing: Theme.spacing.small
                LogosText {
                    id: phaseSym
                    text: playerBar.caching ? "◌" : "▶"
                    color: playerBar.caching ? root.cachingYellow : root.accent
                    font.pixelSize: Theme.typography.secondaryText; Layout.preferredWidth: 10
                    horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignVCenter
                    SequentialAnimation {
                        id: breathe; running: playerBar.caching; loops: Animation.Infinite
                        NumberAnimation { target: phaseSym; property: "opacity"; from: 1.0; to: 0.35; duration: 600 }
                        NumberAnimation { target: phaseSym; property: "opacity"; from: 0.35; to: 1.0; duration: 600 }
                        onRunningChanged: if (!running) phaseSym.opacity = 1
                    }
                }
                LogosText {
                    text: playerBar.caching ? ("Caching… " + root.cacheLeft + "s · " + root.nowPlaying) : root.nowPlaying
                    font.pixelSize: Theme.typography.primaryText; Layout.fillWidth: true
                    elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter
                }
                LogosButton {   // #19: perfect-round stop icon
                    text: "■"
                    implicitWidth: 30; implicitHeight: 30
                    radius: 15      // width/2 → perfect circle
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.stopPlay()
                }
            }
        }

        // ── Activity log ──
        Rectangle {
            Layout.fillWidth: true; height: 96; radius: Theme.spacing.radiusMedium
            color: root.bgSecondary; border.color: root.borderColor; border.width: 1
            ColumnLayout {
                anchors.fill: parent; anchors.margins: Theme.spacing.small; spacing: Theme.spacing.tiny / 2
                LogosText { text: "Activity"; color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.events
                    delegate: LogosText { text: modelData; color: root.textMuted; font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont }
                }
            }
        }
    }
}
