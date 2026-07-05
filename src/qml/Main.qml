import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import Logos.Theme      // logos-design-system (native on RC3+ Basecamp) — skill: logos-design-system-adoption
import Logos.Controls   // LogosText / LogosButton / LogosBadge / LogosSlider / LogosSwitch / LogosTextField

// Receiver — discover & listen to decentralized Logos radio broadcasts (listen-only).
// Binds to its C++ backend (a Qt Remote Objects source) via logos.module("receiver_ui"):
//   PROPs read directly (backend.nodeReady), SIGNALs via Connections, SLOTs called directly.
Item {
    id: root
    anchors.fill: parent

    // #14 monochrome vector pin (Lucide "pin") — stroke follows iconColor; no image plugin needed.
    // Paths live in a 24×24 space; the inner Shape is scaled to fill, the outer Item carries the click.
    component PinIcon: Item {
        id: pin
        property color iconColor: root.textMuted
        implicitWidth: 16; implicitHeight: 16
        Shape {
            anchors.fill: parent
            transform: Scale { xScale: pin.width / 24; yScale: pin.height / 24 }
            ShapePath {
                strokeColor: pin.iconColor; strokeWidth: 2; fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M12 17v5" }
            }
            ShapePath {
                strokeColor: pin.iconColor; strokeWidth: 2; fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z" }
            }
        }
    }
    // Lucide "play" — rounded triangle. filled=true → solid (button affordance); else outline.
    component PlayIcon: Item {
        id: pl
        property color iconColor: root.textMuted
        property bool filled: false
        implicitWidth: 14; implicitHeight: 14
        Shape {
            anchors.fill: parent
            transform: Scale { xScale: pl.width / 24; yScale: pl.height / 24 }
            ShapePath {
                strokeColor: pl.iconColor; strokeWidth: 2; fillColor: pl.filled ? pl.iconColor : "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z" }
            }
        }
    }
    // Lucide "square" — the stop control. Filled + rounded (same style as the filled play).
    component StopIcon: Item {
        id: sq
        property color iconColor: root.textMuted
        implicitWidth: 14; implicitHeight: 14
        Shape {
            anchors.fill: parent
            transform: Scale { xScale: sq.width / 24; yScale: sq.height / 24 }
            ShapePath {
                strokeColor: sq.iconColor; strokeWidth: 2; fillColor: sq.iconColor
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M6.5 4h11a2.5 2.5 0 0 1 2.5 2.5v11a2.5 2.5 0 0 1-2.5 2.5h-11A2.5 2.5 0 0 1 4 17.5v-11A2.5 2.5 0 0 1 6.5 4z" }
            }
        }
    }

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
    // #35 self-match-safe reap for an orphaned stream — the [n]/[c] brackets stop the pattern from matching
    // the shell that runs it, while still matching the receiver's Tor (receiver_ui/torlisten) + ffplay (cookieCheck).
    readonly property string reapCmd: "pkill -f 'receiver_ui/torliste[n]|ffplay.*cookieChe[c]k'"

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
    // #32 secondary line for a station (shared by the list row AND the player bar):
    //   onion → "Anonymous over Tor" (or "<host> over Tor"); otherwise "<host> · <privacy>".
    // #13/#24 secondary line. onion → "IP hidden by Tor" (a persistent fingerprint makes a station
    // pseudonymous, not anonymous, so this is the honest framing); append " · <pgp words>" when the
    // announce is signed + verified. Direct → "<host> · <privacy>" + the words when verified.
    function hostLine(host, privacy, fingerprint) {
        var fp = (fingerprint && fingerprint.length) ? " · " + fingerprint : ""
        if ((privacy || "").toLowerCase() === "onion")
            return "IP hidden by Tor" + fp
        var h = (host && host.length) ? host : "anonymous"
        return h + (privacy ? " · " + privacy : "") + fp
    }
    function playingHostLine() {
        var ss = root.stations()
        for (var i = 0; i < ss.length; i++)
            if (ss[i].name === root.nowPlaying) return root.hostLine(ss[i].host, ss[i].privacy, ss[i].fingerprint)
        return "IP hidden by Tor"
    }
    // #40 current show of the station we're playing (for the player bar)
    function playingNowText() {
        var ss = root.stations()
        for (var i = 0; i < ss.length; i++)
            if (ss[i].name === root.nowPlaying) return ss[i].nowPlaying || ""
        return ""
    }

    readonly property string status:      backend ? backend.connectionStatus : "no backend"
    readonly property bool   nodeReady:    backend ? backend.nodeReady    : false
    readonly property bool   discovering:  backend ? backend.discovering  : false
    readonly property string torStatus:    backend ? backend.torStatus    : "off"
    readonly property string nowPlaying:   backend ? backend.nowPlaying   : ""
    readonly property int    listenBuffer: backend ? backend.listenBuffer : 20
    readonly property bool   hideCache:    backend ? backend.hideCache    : false

    // #44 a private topic REPLACES the view: selectedTopic ("" = public directory) → activeTopic filters the list
    property string selectedTopic: ""
    readonly property string activeTopic: selectedTopic.length > 0 ? selectedTopic : (backend ? backend.publicTopic : "")
    // #4 friendly label for the directory indicator ("Public" or the private directory's <id>)
    function directoryLabel() {
        if (root.selectedTopic.length === 0) return "Public"
        var m = root.selectedTopic.match(/\/radio-basecamp\/1\/([^/]+)\/json/)
        return m ? m[1] : root.selectedTopic
    }
    function stations() {
        if (!backend) return []
        var all
        try { all = JSON.parse(backend.stationsJson) } catch (e) { return [] }
        var at = root.activeTopic
        return all.filter(function(s) { return (s.topic || "") === at })
    }
    // #14 pinned stations (by pubkey), online/offline — survives reload
    readonly property var pinnedList: {
        if (!backend) return []
        try { return JSON.parse(backend.pinnedJson) } catch (e) { return [] }
    }
    function isPinned(pubkey) {
        if (!pubkey) return false
        for (var i = 0; i < root.pinnedList.length; i++) if (root.pinnedList[i].pubkey === pubkey) return true
        return false
    }
    function togglePin(pubkey) {
        if (!backend || !pubkey) return
        if (root.isPinned(pubkey)) logos.watch(backend.unpinStation(pubkey), function(){}, function(){})
        else logos.watch(backend.pinStation(pubkey), function(){}, function(){})
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

            // #37 Tor service badge — listener Tor health (visible once spawned/pre-warmed)
            LogosBadge {
                visible: root.torStatus !== "off"
                Layout.alignment: Qt.AlignVCenter
                text:  root.torStatus === "ready"  ? "tor ready"
                     : root.torStatus === "failed" ? "tor failed" : "tor booting"
                color: root.torStatus === "ready"  ? root.ok
                     : root.torStatus === "failed" ? Theme.palette.error : root.standby
            }
            // #37 Player (ffplay) service badge — only while a station is playing
            LogosBadge {
                visible: root.nowPlaying.length > 0
                Layout.alignment: Qt.AlignVCenter
                text:  root.playPhase === "playing" ? "playing"
                     : root.playPhase === "caching" ? "buffering" : "connecting"
                color: root.playPhase === "playing" ? root.ok : root.standby
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

                // #2/#3/#44 Private directory — subscribe to a private directory instead of the public one
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.tiny
                    LogosText { text: "Private directory"; font.pixelSize: Theme.typography.primaryText }
                    LogosText {
                        text: "Paste a private directory to see only its stations. ✕ returns to the public directory."
                        color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Item {
                        id: dirRow
                        Layout.fillWidth: true
                        implicitHeight: dirField.implicitHeight
                        readonly property string tt: dirField.text.trim()
                        readonly property bool active: tt.length > 0 && tt === root.selectedTopic
                        function submit() { if (backend && dirRow.tt.length) { backend.addTopic(dirRow.tt); root.selectedTopic = dirRow.tt } }
                        LogosTextField {
                            id: dirField
                            width: parent.width
                            placeholderText: "Private directory (/radio-basecamp/1/<id>/json)"
                        }
                        LogosButton {
                            visible: dirRow.tt.length > 0 && !dirRow.active
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Theme.spacing.tiny }
                            text: "Switch"; implicitHeight: Math.max(24, dirField.implicitHeight - 8)
                            onClicked: dirRow.submit()
                        }
                        LogosText {
                            visible: dirRow.active
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Theme.spacing.small }
                            text: "✕"; font.pixelSize: Theme.typography.secondaryText
                            color: dirClearArea.containsMouse ? root.accent : root.textMuted
                            MouseArea { id: dirClearArea; anchors.fill: parent; anchors.margins: -6
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { dirField.text = ""; root.selectedTopic = "" } }
                        }
                    }
                    Connections {
                        target: dirField.textInput
                        function onAccepted() { var t = dirField.text.trim(); if (backend && t.length) { backend.addTopic(t); root.selectedTopic = t } }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

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

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                // #35 orphan-stream guidance (subtle) — ffplay+Tor keep running if you close the module
                // mid-play (#2/#10). Warn to Stop first; give a copy-able reap command for when you forget.
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.tiny
                    LogosText {
                        text: "⚠ Stop the station before closing this module — otherwise the stream keeps playing in the background."
                        color: root.textPrimary; font.pixelSize: Theme.typography.primaryText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    LogosText {
                        text: "Forgot? Reap the orphaned stream from a terminal:"
                        color: root.textMuted; font.pixelSize: Theme.typography.secondaryText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Rectangle {                       // command box + copy icon
                        Layout.fillWidth: true
                        implicitHeight: cmdRow.implicitHeight + Theme.spacing.small
                        radius: Theme.spacing.radiusSmall
                        color: root.bgPrimary; border.color: root.borderColor; border.width: 1
                        RowLayout {
                            id: cmdRow
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                      leftMargin: Theme.spacing.small; rightMargin: Theme.spacing.small }
                            spacing: Theme.spacing.small
                            LogosText {
                                text: root.reapCmd
                                font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont
                                color: root.textSecondary
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            Rectangle {               // copy icon — matches the Activity-log copy button
                                id: reapCopyBtn
                                implicitWidth: 20; implicitHeight: 20; color: "transparent"
                                opacity: reapCopyArea.containsMouse ? 0.9 : 0.5
                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                Rectangle { x: 3; y: 6; width: 10; height: 10; color: "transparent"; border.color: root.textMuted; border.width: 1; radius: 2 }
                                Rectangle { x: 6; y: 3; width: 10; height: 10; color: root.bgPrimary; border.color: root.textMuted; border.width: 1; radius: 2 }
                                Timer { id: reapCopyFb; interval: 200; onTriggered: reapCopyBtn.opacity = reapCopyArea.containsMouse ? 0.9 : 0.5 }
                                MouseArea {
                                    id: reapCopyArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.copyText(root.reapCmd); reapCopyBtn.opacity = 0.25; reapCopyFb.restart() }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── #14 Pinned stations — above the topic search; survive reload; online/offline matched by pubkey ──
        ColumnLayout {
            Layout.fillWidth: true; spacing: Theme.spacing.tiny
            visible: root.pinnedList.length > 0
            LogosText { text: "Pinned stations"; color: root.textMuted; font.pixelSize: Theme.typography.secondaryText }
            Repeater {
                model: root.pinnedList
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: (modelData.online && (modelData.nowPlaying || "").length > 0) ? 66 : 52
                    radius: Theme.spacing.radiusMedium
                    color: pinRowArea.containsMouse && modelData.online ? root.bgPrimary : root.rowBase
                    opacity: modelData.online ? 1.0 : 0.55
                    Rectangle {                          // dot: online → ok, offline → muted
                        id: pdot
                        anchors.left: parent.left; anchors.leftMargin: Theme.spacing.medium
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4; color: modelData.online ? root.ok : root.textMuted
                    }
                    Column {
                        anchors.left: pdot.right; anchors.leftMargin: Theme.spacing.small
                        anchors.right: pinCtl.left; anchors.rightMargin: Theme.spacing.small
                        anchors.verticalCenter: parent.verticalCenter; spacing: 0
                        LogosText { text: (modelData.name || "(unnamed)") + (modelData.online ? "" : " · offline")
                               color: modelData.online ? root.textPrimary : root.textMuted
                               font.pixelSize: Theme.typography.primaryText; width: parent.width; elide: Text.ElideRight }
                        LogosText { text: root.hostLine(modelData.host, modelData.privacy, modelData.fingerprint)
                               color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText; width: parent.width; elide: Text.ElideRight }
                        LogosText { visible: modelData.online && (modelData.nowPlaying || "").length > 0
                               text: "Playing now: " + (modelData.nowPlaying || "")
                               color: root.accent; font.pixelSize: Theme.typography.secondaryText; width: parent.width; elide: Text.ElideRight }
                    }
                    MouseArea {                          // row click → play (only when online)
                        id: pinRowArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: modelData.online ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (modelData.online) root.startPlay(modelData.streamUrl, modelData.name)
                    }
                    Row {                                // right controls — declared last so they're on top of pinRowArea
                        id: pinCtl
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacing.medium
                        anchors.verticalCenter: parent.verticalCenter; spacing: Theme.spacing.small
                        PinIcon {                        // #1 pin LEFT of play — unpin (pinned → accent; hover gray = will remove)
                            anchors.verticalCenter: parent.verticalCenter
                            width: 15; height: 15
                            iconColor: unpinArea.containsMouse ? root.textMuted : root.accent
                            MouseArea { id: unpinArea; anchors.fill: parent; anchors.margins: -6
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.togglePin(modelData.pubkey) }
                        }
                        Rectangle {                      // play — circle green if live, gray if offline
                            anchors.verticalCenter: parent.verticalCenter
                            width: 26; height: 26; radius: 13
                            color: (modelData.online && pinPlayArea.containsMouse) ? root.ok : "transparent"
                            border.width: 1; border.color: modelData.online ? root.ok : root.textMuted
                            PlayIcon {
                                anchors.centerIn: parent; anchors.horizontalCenterOffset: 1
                                width: 12; height: 12; filled: true
                                iconColor: !modelData.online ? root.textMuted
                                         : (pinPlayArea.containsMouse ? root.bgPrimary : root.ok)
                            }
                            MouseArea {
                                id: pinPlayArea; anchors.fill: parent; hoverEnabled: true; enabled: modelData.online
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.startPlay(modelData.streamUrl, modelData.name)
                            }
                        }
                    }
                }
            }
        }

        // #4 Directory indicator (H2 subtitle) — input lives in Settings; ✎ (right after the name) opens it
        RowLayout {
            Layout.fillWidth: true; spacing: Theme.spacing.small
            LogosText {
                text: "Directory: " + root.directoryLabel()
                color: root.textPrimary
                font.pixelSize: Theme.typography.panelTitleText; font.weight: Theme.typography.weightBold
                elide: Text.ElideRight
            }
            LogosText {                              // ✎ edit → open Settings — right after the label, not far right
                text: "✎"; font.pixelSize: Theme.typography.primaryText
                color: dirEditArea.containsMouse ? root.accent : root.textMuted
                Layout.alignment: Qt.AlignVCenter
                MouseArea {
                    id: dirEditArea; anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsOpen = true
                }
            }
            Item { Layout.fillWidth: true }          // filler keeps label + ✎ left-aligned
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
                    width: list.width; radius: Theme.spacing.radiusMedium
                    height: (modelData.nowPlaying || "").length > 0 ? 66 : 52   // #40 taller for the now-playing line
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
                        Rectangle {                     // circular play button — unified green (matches pinned/live)
                            visible: !statusText.active
                            anchors.centerIn: parent; width: 26; height: 26; radius: 13
                            color: rowArea.containsMouse ? root.ok : "transparent"
                            border.width: 1; border.color: root.ok
                            PlayIcon {
                                anchors.centerIn: parent; anchors.horizontalCenterOffset: 1  // optical nudge
                                width: 12; height: 12; filled: true
                                iconColor: rowArea.containsMouse ? root.bgPrimary : root.ok
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
                        LogosText {                     // #40 now-playing (swapped above the identity line)
                               visible: (modelData.nowPlaying || "").length > 0
                               text: "Playing now: " + (modelData.nowPlaying || "")
                               color: root.accent; font.pixelSize: Theme.typography.secondaryText; width: parent.width; elide: Text.ElideRight }
                        LogosText { text: root.hostLine(modelData.host, modelData.privacy, modelData.fingerprint)
                               color: root.textSecondary; font.pixelSize: Theme.typography.secondaryText; width: parent.width; elide: Text.ElideRight }
                    }
                    MouseArea {
                        id: rowArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // #26 hovering a station = intent to play → pre-build its Tor circuit so Play is fast
                        onEntered: if (backend) backend.prewarm(modelData.streamUrl)
                        onClicked: root.startPlay(modelData.streamUrl, modelData.name)
                    }
                    // #14 pin toggle — verified stations only (pin anchors on pubkey). Declared after rowArea so
                    // it sits on top and captures its own click without triggering play.
                    PinIcon {                        // #6 gray by default → orange when pinned or hovered
                        visible: (modelData.pubkey || "").length > 0
                        anchors.right: statusText.left; anchors.rightMargin: Theme.spacing.small
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15; height: 15
                        iconColor: (root.isPinned(modelData.pubkey) || pinArea.containsMouse) ? root.accent : root.textMuted
                        MouseArea {
                            id: pinArea; anchors.fill: parent; anchors.margins: -6
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.togglePin(modelData.pubkey)
                        }
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
                            ? (root.discovering ? "Looking for stations…" : "Starting discovery…")
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
                Item {
                    id: phaseSym
                    implicitWidth: 12; implicitHeight: 12
                    Layout.preferredWidth: 12; Layout.alignment: Qt.AlignVCenter
                    transformOrigin: Item.Center
                    LogosText {                            // connecting → breathing filled dot
                        anchors.centerIn: parent; visible: !playerBar.live
                        text: "●"; color: root.cachingYellow; font.pixelSize: Theme.typography.secondaryText
                        horizontalAlignment: Text.AlignHCenter
                    }
                    PlayIcon {                             // playing → vector play (matches the list)
                        anchors.centerIn: parent; visible: playerBar.live
                        width: 11; height: 11; filled: true; iconColor: root.accent
                    }
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
                    LogosText {                    // #40 now-playing — swapped above the identity line
                        visible: playerBar.live && root.playingNowText().length > 0
                        text: "Playing now: " + root.playingNowText()
                        color: root.accent; font.pixelSize: Theme.typography.secondaryText
                        width: parent.width; elide: Text.ElideRight
                    }
                    LogosText {                    // identity/host when playing, rotating msg while connecting
                        text: playerBar.live ? root.playingHostLine()
                            : root.connectMsgs[root.connectMsgIndex % root.connectMsgs.length]
                        color: playerBar.live ? root.textSecondary : root.cachingYellow
                        font.pixelSize: Theme.typography.secondaryText
                        width: parent.width
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                }
                // #38 small animated soundwave near the stop button — centred rounded bars, wave-shaped
                // (tall in the middle), gently bouncing while playing. Decorative (no real FFT).
                Row {
                    id: wave
                    visible: playerBar.live
                    Layout.alignment: Qt.AlignVCenter
                    height: 28; spacing: 2
                    property real phase: 0
                    // one animated phase drives all bars; ×2 (integer) → the loop wraps seamlessly.
                    NumberAnimation on phase { running: wave.visible; from: 0; to: 6.2832; duration: 900; loops: Animation.Infinite }
                    Repeater {
                        model: 11
                        delegate: Rectangle {
                            width: 3; radius: width / 2
                            anchors.verticalCenter: parent.verticalCenter    // grows both ways from the centre line
                            color: root.accent
                            readonly property real d: Math.abs(index - 5)     // distance from the centre bar → symmetric ripple
                            // full-range amplitude, rippling outward from the centre (bars at equal d move together)
                            height: 3 + (wave.height - 3) * (0.5 + 0.5 * Math.sin(wave.phase * 2 - d * 0.85))
                        }
                    }
                }
                LogosButton {   // #19: perfect-round stop button, Lucide "square" glyph
                    id: stopBtn
                    text: ""
                    implicitWidth: 30; implicitHeight: 30
                    radius: 15      // width/2 → perfect circle, not a rounded rect
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: root.stopPlay()
                    StopIcon {
                        anchors.centerIn: parent
                        width: 12; height: 12
                        iconColor: stopBtn.hovered ? root.accent : root.textPrimary
                    }
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
