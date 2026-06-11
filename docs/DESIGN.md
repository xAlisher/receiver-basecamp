# receiver-basecamp — Design

Status: **kickstart draft** (2026-06-11). Companion to [`BRIEF.md`](BRIEF.md). Read the brief first
for the "why" and the unlock; this doc is the "how".

---

## 1. Architecture: one `ui_qml` module with a C++ backend

The single most important decision. **Do not** use the tutorial-v3 "core module + QML UI" split — that
is the exact shape that crashes on delivery (#31/#150). Use the **`logos-delivery-demo` shape**: one
module, `type: ui_qml`, whose C++ backend runs in the token-privileged `ui-host` process.

```
┌─────────────────────────── ui-host process (has capability token) ───────────────────────────┐
│                                                                                                │
│   receiver_ui  (type: ui_qml)                                                                  │
│   ├─ Main.qml ............... Listen list + player bar + settings pane (QML, sandboxed)         │
│   └─ C++ backend (.so) ...... ReceiverPlugin                                                    │
│         ├─ delivery client ... getClient("delivery_module")  → subscribe + messageReceived      │
│         │                       (WORKS here — ui-host is token-privileged)                      │
│         ├─ station registry .. ingest announces, TTL prune, expose to QML                       │
│         └─ player ............ QProcess: torsocks ffplay (.onion) / ffplay (direct)             │
│                                                                                                 │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
                      │ getClient / invokeRemoteMethod (same-process or token-provisioned)
                      ▼
            delivery_module  (platform-provided; Waku relay)
```

QML talks to its **own** backend (same module — the `.rep` remote-object interface exposes
slots/signals), **not** `logos.callModule` to a separate module. Every backend method returns a JSON
string (platform convention); QML parses it.

**Why not pure QML calling `logos.callModule("delivery_module", …)`?** Two blockers: (a) the QML bridge
is request/response — receiving the async `messageReceived` event stream in pure QML is unreliable;
(b) playback needs a subprocess (`ffplay`), which the QML sandbox forbids. A C++ backend is required
regardless, so it owns delivery too.

### Module manifest (shape)

```json
{
  "name": "receiver_ui",
  "type": "ui_qml",
  "view": "Main.qml",
  "dependencies": ["delivery_module"]
}
```

Scaffold with **`logos-module-builder` (`mkLogosQmlModule`)** — the only supported scaffold path
(see basecamp-skills `logos-module-builder-scaffold`, `builder-ui-qml-adoption`). The C++ backend is the
QML module's native plugin (same pattern as delivery-demo's `logos_delivery_demo_plugin.cpp` +
`logos_delivery_demo.rep`).

---

## 2. The delivery recipe (exact, latest-platform-safe)

`delivery_module` API (v0.1.2, from `refs/logos-delivery-module/src/delivery_module_plugin.h`):

| Method | Sig | Notes |
|---|---|---|
| `createNode(cfg: QString)` | `LogosResult` | JSON config, call **once** per context |
| `subscribe(contentTopic)` / `unsubscribe(contentTopic)` | `LogosResult` | |
| `send(contentTopic, payload)` | `LogosResult` | not used by receiver (listen-only) |
| event `messageReceived` | `QVariantList` | fired on a subscribed topic; `data[2]` = base64 payload |

**Backend wiring — use the typed `LogosModules` SDK, mirroring `logos-delivery-demo` exactly** (this is
the corrected, proven pattern; raw `getClient`/`invokeRemoteMethod` is *not* what the working demo does):

```cpp
void ReceiverUiPlugin::initLogos(LogosAPI* api) {
    if (m_logos) return;
    m_logosAPI = api;
    m_logos = new LogosModules(api);   // SAFE in ui-host (the exact call that crashes from a core sidecar)
    setBackend(this);                  // wire the .rep source → QML
    wireEvents();
    // delivery-demo creates the node from the UI; for a receiver we auto-start discovery once,
    // deferred so the event loop + QRO registry have settled (skill: ipc-client-eager-init).
    QTimer::singleShot(2500, this, [this]{ startDiscovery(); });
}

void ReceiverUiPlugin::wireEvents() {
    m_logos->delivery_module.on("connectionStateChanged", [this](const QVariantList& d){
        if (!d.isEmpty()) setConnectionStatus(d.at(0).toString());
    });
    m_logos->delivery_module.on("messageReceived", [this](const QVariantList& d){
        if (d.size() < 3) return;
        ingestAnnounce(d.at(2));        // d: [0]=hash,[1]=topic,[2]=payload,[3]=ts(ns) — see decode note
    });
}

QString ReceiverUiPlugin::startDiscovery() {              // a .rep SLOT (returns "" on success, else error)
    if (!m_logos) return QStringLiteral("backend not initialised");
    if (!nodeReady()) {
        QJsonObject cfg{{"logLevel","INFO"},{"mode","Core"},{"preset","logos.dev"},{"relay",true}};
        LogosResult c = m_logos->delivery_module.createNode(
            QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
        if (!c.success) { setLastError(c.getError()); return c.getError(); }
        LogosResult s = m_logos->delivery_module.start();
        if (!s.success) { setLastError(s.getError()); return s.getError(); }
        setNodeReady(true);
        LogosResult v = m_logos->delivery_module.getNodeInfo(QStringLiteral("Version"));
        if (v.success) setDeliveryVersion(v.getString());
    }
    m_logos->delivery_module.subscribe(directoryTopic());  // /radio-basecamp/1/directory/json
    setDiscovering(true);
    return QString();
}
```

**Rules carried over from hard-won skills (don't re-derive):**
- Construct `new LogosModules(api)` **in `initLogos`** (delivery-demo does — it's cheap), call
  `setBackend(this)`, then `wireEvents()`. The crash is **process identity, not timing** — this is safe
  in a `ui_qml` backend (ui-host) and forbidden in a `type: core` sidecar.
- `preset: "logos.dev"` + `relay: true` to **interop with live `radio-basecamp` hosts** (e.g. Sneg's
  "Logos manifesto"); the demo defaults `logos.test` — we must match the host's fleet to discover it.
- **messageReceived decode (interop):** radio hosts `send` the announce **JSON string**; on receipt
  `data[2]` is a **base64** string → `QByteArray::fromBase64(...)` → JSON (radio's proven single decode).
  Make `ingestAnnounce` robust (try base64→JSON, fall back to raw UTF-8) and **lock it down with runtime
  proof in M3** — the typed-SDK `QVariant` form (string vs bytes) is the one thing to verify live.
- If a delivery call returns an empty `LogosResult` cross-process, fall back to a `*Json` wrapper
  (skill: `logosresult-json-wrapper-ipc`).

---

## 3. Announce schema — interop with `radio-basecamp` hosts

receiver MUST speak the **same topic + payload** that `radio-basecamp` hosts emit, or it won't discover
the live stations (e.g. "Logos manifesto" on Sneg). Reuse radio's schema verbatim:

- **Public directory topic:** `/radio-basecamp/1/directory/json`
- **Private topics:** `/radio-basecamp/1/<path>/json` (user pastes these via "+ Add a private topic")
- **Announce payload** (base64-decoded JSON), per radio's `buildAnnouncePayload`:
  `{ name, hostLabel, streamUrl (http://<onion>/<path>/index.m3u8 | direct LAN), privacy, path,
     announceTopic, ... }` + a heartbeat every **15 s**; prune a station after **45 s** (3 missed beats).

Lift `ingestAnnounce` / station-registry / TTL logic directly from
`radio-basecamp/radio_module/src` (the listener half). Keep version `…/1/…` so both sides match.

> Action item: copy the exact field list from `radio-basecamp` `buildAnnouncePayload`/`ingestAnnounce`
> when implementing, so there's zero schema drift.

---

## 4. Playback (lift from radio — already proven cross-machine)

`play(streamUrl)` runs `ffplay` via `QProcess`. Reuse radio's listener code wholesale; the hard bugs
are already solved there (PROJECT_KNOWLEDGE §Tor onion mode):

- **Security seam:** allow-list `http://` / `https://` only; reject `file:` / `pipe:` / `concat:` so an
  attacker-controlled `streamUrl` is safe to hand to ffplay. A `.onion` http URL passes unchanged.
- **Onion playback:** `torsocks ffplay` + `-cookies "cookieCheck=1; path=/"` (MediaMTX gates HLS with a
  Secure cookie that ffmpeg won't return over the http onion → otherwise silent 302 loop / no audio).
- **Jitter buffer:** `-infbuf -live_start_index -<bufferSec>` (mpegts HLS) to ride out Tor latency;
  expose `setListenBuffer()` (2–20 s). Listener-side only.
- **Spawn env:** strip `LD_LIBRARY_PATH`/`LD_PRELOAD` for the spawned system `tor`/`ffplay`
  (`cleanSpawnEnv()`), or the AppImage's libs poison them (skill: `appimage-child-ld-library-path`).
- No Qt Multimedia in the AppImage → ffplay is the only path. `ffprobe` optional for metadata.

### Backend API surface (all return JSON strings)

`startDiscovery()` · `stopDiscovery()` · `addTopic(topic)` · `getStations()` (array w/ name, host,
uptime, privacy) · `play(streamUrl)` · `stop()` · `setVolume(v)` · `setListenBuffer(sec)` · `status()`.
This is the **Listen half** of radio's interface — none of `startStream/stopStream/regenerateKey/
regenerateOnion/writeMediaMtxConfig/spawnMediaMtx/ensureTorHost`.

---

## 5. Cross-platform & packaging

The reason this ships where radio can't:

- **Runtime deps:** `tor`, `torsocks`, `ffmpeg`/`ffplay` only — all available on macOS & arm64.
  **Drop `mediamtx`** from `metadata.json` `nix.packages.runtime` (mediamtx is host-only and the binary
  that blocks arm/darwin cross-builds).
- **Variants:** target `linux-amd64`, `linux-arm64`, `darwin-arm64`. Provide `-dev` variant keys so
  `logoscore` headless tests load it (skill: `logoscore-headless-testing`).
- **Portable `.lgx`:** bundle transitive libs with `$ORIGIN` rpath (`.lgx-portable`) so `dlopen` is
  ~300–600 ms and the **2 s token-handshake** doesn't time out on cold FS
  ([basecamp#169](https://github.com/logos-co/logos-basecamp/issues/169)). Unbundled Nix-store dev
  builds can take 3–10 s and get SIGTERM'd. (Until a configurable timeout lands, portable bundling is
  the workaround.)
- **Catalog:** publish to the `xAlisher/logos-basecamp-modules` repo (lgpd install one-liner).

**Validated 2026-06-11 (was the one open unknown — now resolved):** the current platform
(`pre-release-63b35e8-295`) ships **macOS/arm64** (`…aarch64-unsigned.app.tar.gz`), **linux-arm64**
(`…aarch64.AppImage`), and **linux-x86_64** (`…x86_64.AppImage`). `delivery_module` builds for macOS
(CI matrix `ubuntu-latest` + `macos-latest`) and ships `.dylib` + `.so` native libs (per its
`metadata.json` `include`). So all three target arches are real today. The only residual is a packaging
check: confirm `delivery_module` is **provided** (catalog/bundle) per-arch on a fresh macOS install so
`dependencies: ["delivery_module"]` resolves — it demonstrably *builds* for mac.

---

## 6. Risks & how each is retired

| Risk | Mitigation / proof |
|---|---|
| getClient(delivery) crash (#31) | Consume delivery **only in the ui_qml C++ backend (ui-host)**. Proven by `logos-delivery-demo`. |
| Token bootstrap timeout (#150) | ui-host has the bootstrap token; raw `invokeRemoteMethod` from a core sidecar is what fails — we don't do that. |
| 2 s handshake SIGTERM on dev `.lgx` (#169) | Portable `$ORIGIN`-bundled `.lgx`. |
| delivery not on arm/darwin | **Resolved (§5):** platform ships mac-arm64/linux-arm64/linux-x64; delivery CI builds ubuntu+macos. Residual = packaging-provided check only. |
| Schema drift vs radio hosts | Copy radio's exact topic + announce fields; share the `…/1/…` version string. |
| Onion playback silently no-audio | cookieCheck cookie + clean spawn env + jitter buffer (all already solved in radio). |
| LogosResult empty cross-process | `*Json` wrapper fallback (skill `logosresult-json-wrapper-ipc`). |

## 7. Milestone plan (P0 = the demo)

| # | Milestone | Proves |
|---|---|---|
| 1 | Scaffold `receiver_ui` (`mkLogosQmlModule`), buildable, loads headless under logoscore | shape compiles + dispatches |
| 2 | Delivery init in ui-host backend: createNode/start/subscribe, **no crash on latest** | the unlock works on 295+ |
| 3 | `ingestAnnounce` + station registry + TTL prune (lift from radio) | discovery of real announces |
| 4 | Listen UI: list renders, tap-to-play; player bar | end-to-end UI |
| 5 | Playback: torsocks ffplay + cookie + buffer (lift from radio) | **hears the Sneg "Logos manifesto" station** |
| 6 | Settings pane (buffer slider) — also closes radio#19 pattern | polish |
| 7 | macOS/arm + linux-arm64 build (platform + delivery confirmed cross-arch, §5) | the Mac win |
| 8 | Catalog release + README | shippable |

**P0 vertical slice = #1–#5:** on a current Basecamp, discover and play the live station with no 268 pin.

## 8. What to reuse vs. write fresh

- **Reuse (copy from `radio-basecamp`):** announce schema, `ingestAnnounce`, station registry + TTL,
  the entire `play/stop/setVolume/setListenBuffer` + torsocks/cookie/buffer/clean-spawn playback path,
  the http(s)-only allow-list, the Listen-tab QML.
- **Write fresh:** the `ui_qml`+C++ backend skeleton (delivery-demo shape), delivery wiring in
  `initLogos` (deferred getClient in ui-host), the cross-arch `flake.nix`/`metadata.json` (drop mediamtx).
- **Reference repos:** `logos-delivery-demo` (the shape), `refs/logos-delivery-module` (the API),
  `radio-basecamp` `ui-qml-backend` branch (what to do — and the trap of leaving delivery in core).

## 9. Next action

1. ~~Confirm delivery-on-arm/darwin~~ **Done (§5):** mac-arm64/linux-arm64/linux-x64 all ship; delivery
   builds ubuntu+macos. (Only residual: confirm delivery is *provided* per-arch on a fresh mac install.)
2. `logos-module-builder` scaffold `receiver_ui` and land milestone #2 (delivery init in the ui_qml
   backend, no crash on latest `295`) — that single milestone validates the whole thesis. Everything
   after is lifting proven radio code into the new shell.
</content>
