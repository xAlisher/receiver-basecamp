import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme      // logos-design-system (native on RC3+ Basecamp) — skill: logos-design-system-adoption
import Logos.Controls   // LogosText / LogosButton / LogosBadge / LogosSlider / LogosSwitch / LogosTextField

// Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only).
// Binds to its C++ backend (a Qt Remote Objects source) via logos.module("receiver_ui"):
//   PROPs read directly (backend.nodeReady), SIGNALs via Connections, SLOTs called directly.
Item {
    id: root
    anchors.fill: parent

    readonly property var backend: (typeof logos !== "undefined" && logos.module)
                                   ? logos.module("receiver_ui") : null

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
    readonly property color standby:      Theme.palette.warning          // partial/connecting amber
    readonly property color rowBase:      Theme.palette.surfaceRecessed  // recessed row inset inside the panel
    // Monospace family for code-like values (topics, buffer read-out, activity trace). The design system
    // ships no mono token, so keep the generic family here — Qt maps it to the platform fixed-pitch font.
    readonly property string monoFont:    "monospace"

    property bool settingsOpen: false
    property var  events: []

    // #9 true play-state, DERIVED from the backend (never a timer):
    //   connecting = Tor rendezvous, no bytes yet · caching = bytes arriving (aq>0) · playing = audio out (clock)
    readonly property string playPhase:
        (!backend || backend.nowPlaying.length === 0) ? "idle"
        : backend.playbackLive ? "playing"
        : backend.buffering    ? "caching"
        : "connecting"
    readonly property color cachingYellow: Theme.palette.warning

    // #32 player-bar line 2 while connecting: reassurance messages (Tor/p2p/patience), SHUFFLED —
    // a shuffle-bag so each shows once per pass in random order, then re-shuffles (no repeats within a pass).
    property int connectMsgIndex: 0
    property var msgBag: []
    property int msgBagPos: 0
    function reshuffleMsgs() {
        var a = []
        for (var i = 0; i < root.connectMsgs.length; i++) a.push(i)
        for (var j = a.length - 1; j > 0; j--) {          // Fisher–Yates
            var k = Math.floor(Math.random() * (j + 1))
            var t = a[j]; a[j] = a[k]; a[k] = t
        }
        root.msgBag = a; root.msgBagPos = 0
        if (a.length) root.connectMsgIndex = a[0]
    }
    function nextMsg() {
        root.msgBagPos = root.msgBagPos + 1
        if (root.msgBagPos >= root.msgBag.length) reshuffleMsgs()   // full pass done → re-shuffle
        else root.connectMsgIndex = root.msgBag[root.msgBagPos]
    }
    readonly property var connectMsgs: [
        // — the vibe: patience over an onion connection —
        "Tor connection can be slow — that's what protects the streamer's privacy.",
        "Connecting, hang loose.",
        "The only good system is a sound system.",
        "Radio waves are owned by governments — that's why we went p2p.",
        "No central server means decentralisation (and patience while we connect).",
        "Routing through onion layers — three hops for your anonymity.",
        "No middlemen, no ads, no logs — just the signal.",
        "Building a circuit through volunteers' relays worldwide.",
        "Slow radio is free radio.",
        "Nobody knows who's listening. Not even us. That's the point.",
        "Can't be deplatformed if there's no platform.",
        "Handshaking with the hidden service.",
        "Patience is a small price for a station no one can shut down.",
        "The revolution will not be centralised.",
        // — this module's lineage —
        "This module is inspired by “Farewell to Westphalia”, by Jarrad Hope & Peter Ludlow.",
        "Farewell to Westphalia: exit the nation-state, enter the network.",
        // — the OG cypherpunks —
        "“Cypherpunks write code.” — Eric Hughes",
        "“Privacy is necessary for an open society in the electronic age.” — Eric Hughes",
        "“We must defend our own privacy if we expect to have any.” — Eric Hughes",
        "“Encryption is fundamentally a private act.” — Eric Hughes",
        "“Privacy in an open society requires anonymous transaction systems.” — Eric Hughes",
        "“The Net interprets censorship as damage and routes around it.” — John Gilmore",
        "“A specter is haunting the modern world — the specter of crypto anarchy.” — Timothy May",
        "“Arise, you have nothing to lose but your barbed wire fences!” — Timothy May",
        "“Cryptography is the ultimate form of non-violent direct action.” — Julian Assange",
        "“The universe believes in encryption.” — Julian Assange",
        "“If privacy is outlawed, only outlaws will have privacy.” — Phil Zimmermann",
        "“Trusted third parties are security holes.” — Nick Szabo"
    ]
    // #32 player-bar line 2 while playing: the station's host label + privacy (matches the list row)
    function playingHostLine() {
        var ss = root.stations()
        for (var i = 0; i < ss.length; i++)
            if (ss[i].name === root.nowPlaying)
                return (ss[i].host || "anonymous") + " · " + (ss[i].privacy || "")
        return "anonymous"
    }

    readonly property string status:      backend ? backend.connectionStatus : "no backend"
    readonly property bool   nodeReady:    backend ? backend.nodeReady    : false
    readonly property bool   discovering:  backend ? backend.discovering  : false
    readonly property string nowPlaying:   backend ? backend.nowPlaying   : ""
    readonly property int    listenBuffer: backend ? backend.listenBuffer : 20
    readonly property bool   hideCache:    backend ? backend.hideCache    : false

    function stations() {
        if (!backend) return []
        try { return JSON.parse(backend.stationsJson) } catch (e) { return [] }
    }

    function startPlay(url, name) { if (backend) backend.play(url, name) }   // phase derives from the backend
    function stopPlay()           { if (backend) backend.stopPlayback() }

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

    // clipboard helper — the sandbox has no Clipboard API; TextEdit.copy() is the proven path (radio_ui #12)
    function copyText(t) { clipHelper.text = t; clipHelper.selectAll(); clipHelper.copy(); clipHelper.text = "" }
    TextEdit { id: clipHelper; visible: false }

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
            }
            Item { Layout.fillWidth: true }

            // status badge — same design + logic as delivery-demo's LogosBadge, on the delivery node's real,
            // live connection state: Connected→success, PartiallyConnected→warning, Disconnected→error, and
            // neutral textSecondary while starting. (LogosBadge renders AllUppercase; source stays lowercase.)
            LogosBadge {
                id: statusBadge
                Layout.alignment: Qt.AlignVCenter
                text:  root.status === "Connected"         ? "discovering"
                     : root.status === "PartiallyConnected" ? "partial peers"
                     : root.status === "Disconnected"       ? "disconnected"
                     : root.status === "connecting"         ? "connecting…"
                     :                                        "starting…"
                color: root.status === "Connected"         ? root.ok
                     : root.status === "PartiallyConnected" ? root.standby
                     : root.status === "Disconnected"       ? Theme.palette.error
                     : root.status === "connecting"         ? root.standby
                     :                                        root.textSecondary
            }

            // cogwheel — no gear icon asset ships with the module, so a tokenized custom toggle
            // (LogosIconButton needs an iconSource url). Active state borders in the accent colour.
            Rectangle {
                // #31 match the status badge height (keeper cogwheel pattern) — not a hardcoded 28
                implicitWidth: statusBadge.implicitHeight; implicitHeight: statusBadge.implicitHeight
                Layout.alignment: Qt.AlignVCenter
                radius: Theme.spacing.radiusSmall
                color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.settingsOpen ? root.accent : root.borderColor; border.width: 1
                LogosText {
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    text: "⚙"
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
                            text: root.playPhase === "playing" ? "playing"
                                : root.playPhase === "connecting" ? "connecting…" : "caching…"
                            color: root.playPhase === "playing" ? root.accent : root.cachingYellow
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
                        // #26 hovering a station = intent to play → pre-build its Tor circuit so Play is fast
                        onEntered: if (backend) backend.prewarm(modelData.streamUrl)
                        onClicked: root.startPlay(modelData.streamUrl, modelData.name)
                    }
                }

                // empty state
                LogosText {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                    // #34 don't claim "none announced" until the network is actually up (badge not yellow/red)
                    text: (root.status === "Connected" || root.status === "PartiallyConnected")
                            ? (root.discovering ? "Listening for stations…\nnone announced yet" : "Starting discovery…")
                        : root.status === "Disconnected" ? "Disconnected — retrying…"
                        : "Connecting to the network…"
                }
            }
        }

        // ── Player bar (#9: Connecting… / Caching… breathing-yellow → orange ▶ Playing) ──
        Rectangle {
            id: playerBar
            Layout.fillWidth: true; radius: Theme.spacing.radiusMedium; clip: true
            implicitHeight: Math.max(52, barRow.implicitHeight + 2 * Theme.spacing.small)   // grows for wrapped quotes
            Behavior on implicitHeight { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            visible: root.nowPlaying.length > 0
            readonly property bool live: root.playPhase === "playing"   // audio actually out (ffplay clock)
            color: root.bgSecondary; border.width: 1
            border.color: playerBar.live ? root.accent : root.cachingYellow

            RowLayout {
                id: barRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: Theme.spacing.medium; rightMargin: Theme.spacing.medium }
                spacing: Theme.spacing.small
                LogosText {
                    id: phaseSym
                    text: playerBar.live ? "▶" : "●"      // filled dot while connecting, ▶ when playing
                    color: playerBar.live ? root.accent : root.cachingYellow
                    font.pixelSize: Theme.typography.secondaryText; Layout.preferredWidth: 12
                    horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignVCenter
                    transformOrigin: Item.Center
                    // prominent breath: fade + pulse the filled dot until audio is out
                    SequentialAnimation {
                        id: breathe; running: !playerBar.live; loops: Animation.Infinite
                        ParallelAnimation {
                            NumberAnimation { target: phaseSym; property: "opacity"; from: 1.0; to: 0.25; duration: 750; easing.type: Easing.InOutSine }
                            NumberAnimation { target: phaseSym; property: "scale";   from: 1.0; to: 1.5;  duration: 750; easing.type: Easing.InOutSine }
                        }
                        ParallelAnimation {
                            NumberAnimation { target: phaseSym; property: "opacity"; from: 0.25; to: 1.0; duration: 750; easing.type: Easing.InOutSine }
                            NumberAnimation { target: phaseSym; property: "scale";   from: 1.5;  to: 1.0; duration: 750; easing.type: Easing.InOutSine }
                        }
                        onRunningChanged: if (!running) { phaseSym.opacity = 1; phaseSym.scale = 1 }
                    }
                }
                Column {   // #32 two lines, like the station row
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 0
                    LogosText {                    // line 1 — always the station name
                        text: root.nowPlaying
                        font.pixelSize: Theme.typography.primaryText
                        width: parent.width; elide: Text.ElideRight
                    }
                    LogosText {                    // line 2 — host·privacy when playing, rotating msg while connecting
                        text: playerBar.live ? root.playingHostLine()
                            : root.connectMsgs[root.connectMsgIndex % root.connectMsgs.length]
                        color: playerBar.live ? root.textSecondary : root.cachingYellow
                        font.pixelSize: Theme.typography.secondaryText
                        width: parent.width
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                }
                LogosButton {   // #19: perfect-round stop icon
                    text: "■"
                    implicitWidth: 30; implicitHeight: 30
                    radius: 15      // width/2 → perfect circle, not a rounded rect
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.stopPlay()
                }
            }
            // #32 rotate the connecting messages every ~4.5s; random start for variety; only while not live
            Timer {
                id: msgRotate
                interval: 7000; repeat: true   // slow enough to read a full line (incl. wrapped quotes)
                running: playerBar.visible && !playerBar.live
                onRunningChanged: if (running) root.reshuffleMsgs()   // fresh shuffled bag each connect
                onTriggered: root.nextMsg()
            }
        }

        // ── Activity log ──
        Rectangle {
            Layout.fillWidth: true; height: 96; radius: Theme.spacing.radiusMedium
            color: root.bgSecondary; border.color: root.borderColor; border.width: 1
            ColumnLayout {
                anchors.fill: parent; anchors.margins: Theme.spacing.small; spacing: Theme.spacing.tiny / 2
                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.tiny
                    LogosText { text: "Activity"; color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText }
                    Item { Layout.fillWidth: true }
                    // copy-all icon — two overlapping rectangles (keycard ActivityLog style)
                    Rectangle {
                        id: copyBtn
                        visible: root.events.length > 0
                        implicitWidth: 20; implicitHeight: 20; color: "transparent"
                        opacity: copyArea.containsMouse ? 0.9 : 0.5
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                        Rectangle { x: 3; y: 6; width: 10; height: 10; color: "transparent"; border.color: root.textMuted; border.width: 1; radius: 2 }
                        Rectangle { x: 6; y: 3; width: 10; height: 10; color: root.bgSecondary; border.color: root.textMuted; border.width: 1; radius: 2 }
                        Timer { id: copyFb; interval: 200; onTriggered: copyBtn.opacity = copyArea.containsMouse ? 0.9 : 0.5 }
                        MouseArea {
                            id: copyArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { root.copyText(root.events.join("\n")); copyBtn.opacity = 0.25; copyFb.restart() }
                        }
                    }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.events
                    delegate: LogosText { text: modelData; color: root.textMuted; font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont }
                }
            }
        }
    }
}
