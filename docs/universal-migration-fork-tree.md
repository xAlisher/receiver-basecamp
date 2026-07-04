# Universal migration (#20) — fork-tree log

Running log of the red-team push to migrate `receiver_ui` from legacy (Qt `getClient`/QRO) to the
universal API. Methodology: `~/fieldcraft/protocols/red-team-fork-tree.md`. Every idea/step/wall below.
Reports stay in **our** repo (xAlisher) until a fix is proven; upstream (logos-co) only with a clean PR.

**One-line root cause (so far):** the cpp-generator emits **synchronous** method wrappers
(`m_client->invokeRemoteMethod(...)`) that block the single-threaded ui-host event loop — but
delivery's `LogosResult` reply needs that loop to be delivered → **deadlock** on the long-running
`createNode()`. delivery-demo escapes it only by being legacy.

---

## Node 1 — Migrate with builder 0.2.0 (rep + LogosUiPluginContext)
- **Idea:** follow `qml-to-universal-module-qtro-backend` — `interface:universal`, `.rep` + backend
  deriving `ReceiverUiSimpleSource, LogosUiPluginContext`, `getClient/invokeRemoteMethod/onEvent` →
  `modules().delivery_module.*` / `.on(...)`.
- **Move:** rewrote `receiver_ui_plugin.* → receiver_ui_backend.*`; metadata `interface:universal`;
  flake builder `0.2.0`. Commit `98584a6`.
- **Result:** **compiles ✅** — codegen generated `modules().delivery_module` for the *legacy* dep with
  methods AND `.on(event)`.
- **Wall:** runtime — `modules().delivery_module.createNode()` **never returns**; ui-host `State: S`,
  frozen. Node connects server-side (`Disconnected→Connected`, 9 peers) but events never fire.
- **Insight:** compile + wiring are fine; the blocker is a sync-call hang, not the migration shape.

## Node 2 — Reentrancy hypothesis (subscribe AFTER createNode)
- **Idea:** delivery emits `connectionStateChanged` *during* createNode; if we're subscribed, it reenters
  the blocked thread → deadlock. So subscribe events only after createNode returns.
- **Move:** `wireDeliveryEvents()` moved to end of `startDiscovery`, added `createNode` result logging.
  Commit `bd12371`.
- **Result / Wall:** **still hangs**, zero subscriptions active during createNode.
- **Insight:** NOT event reentrancy — the bare synchronous call itself deadlocks.

## Node 3 — Newest master builder
- **Idea:** delivery-demo tracks the newest builder (unpinned) and works; maybe 0.2.0 codegen is stale.
- **Move:** flake builder → `github:logos-co/logos-module-builder` (master `6ef42ea`, 2026-07-01).
- **Result / Wall:** compiles, **same hang**.
- **Insight:** not a version-staleness bug in the qt codegen path; the sync wrapper is the same.

## Node 4 — `bump-cpp-sdk-thread-safe-ipc` builder branch
- **Idea:** our hang is a single-threaded sync-IPC block; a thread-safe-IPC cpp-sdk might deliver the
  reply off-thread.
- **Move:** flake builder → `.../logos-module-builder/bump-cpp-sdk-thread-safe-ipc`.
- **Wall:** **won't compile our structure** — this builder's universal codegen runs
  `logos-cpp-generator --from-header src/<name>_impl.h --impl-class ReceiverUiImpl` (the
  **impl_header / module-context** pattern), replacing `.rep`/`SimpleSource`/`LogosUiPluginContext`.
  No ui_qml reference module exists for it yet (active branch: `extend-universal-modules-with-module-context`).
- **Insight:** the newest universal arch is a different shape; adopting it is a separate rewrite with
  uncertain payoff on the hang.

## Node 5 — Root-cause the generator (patch-the-builder path, user-chosen)
- **Idea:** find *why* the sync wrapper hangs where the legacy `invokeRemoteMethod` returned; fix it in
  the generator so `createNode()` stays synchronous but non-deadlocking.
- **Move:** read `logos-cpp-sdk-generator` source (`cpp-generator/experimental/lidl_gen_client.cpp`).
- **Finding:** for each method the generator emits BOTH:
  - sync: `_result = m_client->invokeRemoteMethod(target, "name", {...}, Timeout(), &_err);` (line ~236) — **blocks**.
  - **async: `<name>Async(..., std::function<void(Ret)> callback, Timeout)`** → `m_client->invokeRemoteMethodAsync(...)` (line ~258) — **already generated, non-blocking**.
  And `logos_api_client.cpp` exposes both `invokeRemoteMethod` and `invokeRemoteMethodAsync`.
- **Insight:** the async escape hatch already exists per-method. Two fixes:
  - (fast, receiver-only) call `modules().delivery_module.createNodeAsync(cfg, cb)` — proves the concept.
  - (proper, all modules) patch the generator's **sync** emission to run `invokeRemoteMethodAsync` under
    a nested `QEventLoop` (spin the loop → reply delivered → return) — "fix the builder".

## Node 6 — Prove async (in progress)
- **Idea:** trivial-experiment-first — use the already-generated `createNodeAsync` in the receiver to
  prove non-blocking createNode works headlessly, before forking the generator.
- **Move:** _in progress._
- **Result:** _pending._

## Node 7 — Patch the generator + fork the builder chain (planned)
- **Idea:** patch `lidl_gen_client.cpp` sync emission → nested-`QEventLoop`-over-async; fork
  logos-cpp-sdk-generator, point our builder fork at it, point the receiver flake at our builder fork.
- **Status:** _planned once Node 6 proves the concept._

---

### Builders tried (flake pins)
| builder | rev | compiles our rep structure | createNode runtime |
|---|---|---|---|
| `0.2.0` | 92ef691 | ✅ | hangs |
| master | 6ef42ea | ✅ | hangs |
| `bump-cpp-sdk-thread-safe-ipc` | — | ❌ (wants impl_header) | n/a |

### Repro (headless)
`nix run .` standalone against Sneg's live "Logos manifesto"; watch `/tmp/receiver-diag.log`:
`onContextReady → fire deferred startDiscovery → startDiscovery: createNode` then **silence** =
the sync createNode hang. A working fix shows `createNode ok=1 → on connectionStateChanged → on messageReceived`.

## Node 6 — Async createNode (result: partial)
- **Move:** `startDiscovery` → `createNodeAsync`/`startAsync`/`subscribeAsync` (chained on callbacks).
- **Result:** async **unblocked** the ui-host (`State: S`, not frozen — proves the sync deadlock is the
  event-loop block). delivery processed createNode server-side: `createNode callback called ret:0`,
  `Delivery context created successfully`.
- **Wall:** the receiver's `createNodeAsync` *reply callback* **never fired** → `start()` (chained behind
  it) never ran → node created but `currentPeerIds=[]`, never connected.
- **Insight:** the async *send* takes effect server-side even though the *reply callback* isn't delivered.
  → don't depend on the callback; **fire-and-forget + time-sequence** create→start→subscribe; discovery
  rides delivery's event-PUSH (`.on`) path, not method replies.

## Node 6b — Fire-and-forget + timed start (testing)
- **Move:** fire `createNodeAsync`, `QTimer::singleShot(3000)` → fire `startAsync` + subscribe + wire
  events. Ignore reply callbacks entirely.
- **Result:** _pending build/run._

## Node 6b — Fire-and-forget + timed start — ✅ WORKS (2026-07-04)
- **Result:** node connects + discovery flows, PROVEN headless against Sneg's live station:
  ```
  createNodeAsync cb: ok=1 · startAsync cb: ok=1
  on connectionStateChanged -> Connected        ← pill green (event push works)
  on messageReceived: d.size=4  (×6)            ← "Logos manifesto" announces ingested → discovered
  ```
  ui-host stays responsive (`State: S`, not frozen). No crash.
- **Why the callbacks fired this time:** once `start()` is actually called, the node fully comes up and
  delivery's createNode/start replies come back (`ok=1`). Previously start() was gated behind the
  createNode callback → circular; firing start() unconditionally (timed) breaks it.

## ✅ ROOT CAUSE (what was wrong)
The cpp-generator's typed `modules().<dep>.method()` **sync** wrapper calls the blocking
`invokeRemoteMethod`, which parks the **single-threaded ui-host event loop**. delivery's `LogosResult`
reply is delivered *through* that loop → it can never arrive → the sync call never returns (deadlock).
The generator already emits an async `*Async` variant whose **send** reaches delivery (the node is
created/started server-side) even when the reply callback is delayed. Discovery itself never needed the
method replies — it rides delivery's **event push** (`.on("messageReceived"/"connectionStateChanged")`).

**Fix (receiver, proven):** fire `createNodeAsync` → (timer 3s) `startAsync` + `subscribeAsync` +
`wireDeliveryEvents`, fire-and-forget. No sync blocking, no reply dependence.

**Upstream fix (the real one, optional):** patch `logos-cpp-sdk` generator's sync method emission
(`experimental/lidl_gen_client.cpp:~236`) to run `invokeRemoteMethodAsync` under a nested `QEventLoop`
so `modules().dep.method()` stays synchronous *and* pumps the loop → reply delivered → returns. That
fixes it for every universal consumer of any Qt/legacy dep, not just the receiver.

## Node 7 — Builder patch: sync wrapper → nested QEventLoop (fork logos-cpp-sdk) — insufficient
- **Fork:** `/extra/tmp/logos-cpp-sdk-fork` (from `logos-co/logos-cpp-sdk` d12a7bb). Patched
  `cpp-generator/experimental/lidl_gen_client.cpp` sync-method emission: run `invokeRemoteMethodAsync`
  under a nested `QEventLoop` (v1), then + a `QTimer` timeout bail using `Timeout().ms` (=20000) so a
  gated reply can't spin forever (v2). Built via `nix run . --override-input
  logos-module-builder/logos-cpp-sdk path:/extra/tmp/logos-cpp-sdk-fork`.
- **Override confirmed applied** (build log shows the fork path + generator rebuild); receiver plugin
  compiled with the patched wrapper.
- **Wall:** sync `createNode()` **still never returns** (>9 min, both v1 and v2). Decisive clue:
  `/proc/<ui-host>/wchan = do_wait` — the thread is blocked **waiting on a child process**, NOT in a
  QEventLoop or socket read. So the block is **inside the IPC consumer** (`invokeRemoteMethodAsync`
  itself blocks before my nested loop ever runs), *deeper* than the generated wrapper.
- **Insight:** the generator patch is the wrong layer. The sync deadlock isn't (only) the wrapper
  choosing to block on the reply — the consumer's async path itself parks on a subprocess wait
  (capability/token bootstrap?). Fixing it means the platform IPC consumer (`logos_api_client` /
  ModuleProxy), not codegen. Per fork-tree methodology: attempt logged, NOT upstreamed (didn't work).

## ✅ SHIPPED SOLUTION
Receiver-side **fire-and-forget** (Node 6b, commit `ec0e850`) — proven end-to-end headless. That's the
working universal migration. The builder patch is parked at Node 7's wall (platform-IPC-consumer issue).
