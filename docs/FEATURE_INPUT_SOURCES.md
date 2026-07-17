# Feature: Boot-Selectable Input Sources — OLV1 (current) & open-dis (new)

Status: **Phases 0-3 complete (2026-07-17)** — §3 decisions below are answered
and folded into §4-§6, which are the **frozen contract** for Phase 1+.
The repo-facing bookkeeping those decisions imply (THIRD_PARTY.md row, SBOM
component) landed with the Phase 2 code.

> **Dependency-strategy change (2026-07-17, user-directed, supersedes §3.1's
> vendoring choice):** `open-dis-cpp` is **no longer vendored** under
> `third_party/`. Instead, `scripts/install_open_dis.sh` fetches the same
> pinned v1.2.0 release (sha256-verified tarball), compiles only the
> self-contained `src/dis6/` tree (verbatim, no local modifications — the
> no-local-changes policy carries over), and installs headers +
> `libopendis6.a` + LICENSE/provenance into a prefix: `/usr/local` inside the
> container build stage (`containers/Containerfile.cpp`), any `--prefix` for
> host builds. `cmake/open_dis_cpp.cmake` locates the install (search:
> `-DOLV_OPEN_DIS_PREFIX`, `/usr/local`, `/opt/open-dis`, `~/.local`) and
> exposes the same `open_dis_cpp` target to backend and simulator. Air-gap
> posture is preserved via `OLV_OPEN_DIS_TARBALL`/`OLV_OPEN_DIS_URL`
> (mirrored tarball; pinned sha256 enforced either way). Every §3.1 mention
> of `third_party/open-dis-cpp/` below is historical.
Companion to docs/PLAN.md (backend threading model, §2/§10) and
docs/PROTOCOL_UDP.md (the existing OLV1 wire format, unchanged by this work).

## 1. Overview

Today `olv_backend` has exactly one hardcoded input path: `UdpReceiver` binds
a UDP socket and calls `olv::proto::decode` (the first-party OLV1 binary
format, `backend/include/olv/protocol.hpp:297`) directly
(`backend/src/udp_receiver.cpp:58`). There is no abstraction boundary between
"receive bytes off the wire" and "decode this specific protocol."

This feature:

1. Introduces an `InputSource` interface so the backend's intake path is a
   pluggable strategy, not a concrete class wired into `main.cpp`.
2. Ships the *existing* OLV1 UDP path as the default implementation of that
   interface, with **zero behavior change** — this is a refactor, not a
   rewrite, and must be indistinguishable from today's behavior when no new
   config is set.
3. Adds a second implementation that ingests **IEEE 1278.1 Distributed
   Interactive Simulation (DIS)** Entity State PDUs using the third-party
   `open-dis-cpp` library, translating them into the same `StateStore`
   contract the OLV1 path already fills.
4. Makes the choice a **boot-time config setting** (`[input] mode` in
   `config/backend.toml`, plus a `--input-mode` CLI flag), following the exact
   precedence rule already used everywhere else in this repo: defaults <
   `--config` file < CLI flags.

Everything downstream of `StateStore` — `WsServer`, the WS JSON wire format
(docs/PROTOCOL_WS.md), and the entire frontend — is **untouched**. This is
intentionally an intake-side-only feature so the blast radius is contained to
`backend/src/` plus new docs/config, mirroring how docs/FEATURE_SKY.md kept
its blast radius to `frontend/js/`.

## 2. Design principles (why it is low-risk)

* **Strategy pattern at one seam.** `InputSource` is the *only* new
  abstraction. `StateStore`, `WsServer`, the WS/HTTP wire formats, and the
  simulator's send-side (`simulator/src/frame_builder.cpp`) are not touched in
  Phases 1-2 (Phase 3 optionally extends the simulator later, additively).
* **Default path is a refactor, not a rewrite.** `Olv1InputSource` must
  produce byte-for-byte identical logging, drop-counting, and `StateStore`
  semantics to today's `UdpReceiver`. The existing integration test and
  `ctest` suite are the regression guard — if either changes behavior under
  `mode=olv1` (the default), that's a bug in the refactor, not an accepted
  change.
* **DIS is additive, not a replacement.** OLV1 is not being deprecated. Both
  paths ship and are selectable; a deployment only ever runs one at a time
  (one bound UDP socket per process, matching the current single-receiver
  model).
* **Uniform observability regardless of mode.** Both implementations call
  the same `StateStore::countReceived` / `countDropped` / `apply` methods, so
  `Stats` (received/accepted/dropped/rate) mean the same thing to an operator
  no matter which input mode is configured.
* **No new dependency until Phase 0 says so.** `open-dis-cpp` is a real new
  third-party dependency in a repo whose stated design goal is "exactly one
  runtime third-party dependency" (THIRD_PARTY.md). Phase 0 exists specifically
  to make that trade-off a deliberate, reviewed decision instead of something
  an implementation agent backs into.

## 3. Phase 0 — Research & decisions (architect, no code changes)

**Complete.** Each item below now carries its frozen **Decision** block;
§4-§6 incorporate them. Research basis: upstream `open-dis/open-dis-cpp`
repo, its `LICENSE` file at head, and the v1.2.0 release (2026-06-26);
`backend/src/state_store.{hpp,cpp}`, `backend/src/udp_receiver.cpp`,
`backend/include/olv/protocol.hpp`, `THIRD_PARTY.md`, `scripts/gen_sbom.py`.

1. **Dependency vetting.** Confirm which `open-dis-cpp` distribution to use
   (canonical repo: `open-dis/open-dis-cpp`), its exact license (verify text
   in the actual release — historically BSD-style), and vendoring strategy.
   Given this repo vendors nothing and expects Boost from the system/toolchain
   (THIRD_PARTY.md), decide between: (a) vendor `open-dis-cpp` source under
   `third_party/open-dis-cpp/` (more consistent with air-gap-friendly, no
   network fetch at build time) vs. (b) `FetchContent`/submodule. **Recommend
   (a)** unless the license or file count makes vendoring impractical.
   Update `THIRD_PARTY.md` and `scripts/gen_sbom.py`/`sbom/backend.cdx.json`
   accordingly (new `library` component, license, purl).

   **Decision (frozen):** use the canonical `open-dis/open-dis-cpp` repo at
   release **v1.2.0** (2026-06-26). License verified against the upstream
   `LICENSE` file: **BSD-2-Clause**, "Copyright (c) 2016, Open DIS. All
   rights reserved." — compatible with this repo's MIT license and air-gap
   posture. **Vendoring: option (a)**, under `third_party/open-dis-cpp/`,
   pruned to what the backend needs:
   - `LICENSE` (verbatim from the v1.2.0 tag) — required by BSD-2-Clause
     clause 1 for source redistribution.
   - `src/dis6/` and `src/utils/` only. The DIS6 tree is self-contained
     (auto-generated via xmlpg, no dependencies beyond the C++ standard
     library); the `dis7/` tree, `examples/` (which need SDL2), `test/`, and
     upstream build files are **excluded** — we compile the vendored sources
     with our own `add_library(open_dis_cpp STATIC ...)` in
     `backend/CMakeLists.txt`, linked only by `olv_backend`.
   - `third_party/open-dis-cpp/README.md` recording: upstream URL, tag
     `v1.2.0`, retrieval date, the pruning rule above, and the local-change
     policy (**none permitted** — any fix goes upstream or waits for a new
     tag; re-vendoring is a wholesale replace of the directory).
   DIS **protocol version 6** classes (`dis6::EntityStatePdu` etc.) are the
   decode path; see item 2 for on-wire version acceptance.
   Bookkeeping lands with the code, not in Phase 0: `THIRD_PARTY.md` gains
   an `open-dis-cpp | Runtime (vendored, static) | BSD-2-Clause` row and
   `scripts/gen_sbom.py` gains a static `library` component
   (`name: open-dis-cpp`, `version: 1.2.0`,
   `purl: pkg:github/open-dis/open-dis-cpp@v1.2.0`, license `BSD-2-Clause`)
   in `build_backend_bom()`, regenerating `sbom/backend.cdx.json` — done in
   Phase 2 (vendoring) and Phase 4 (docs) respectively.
2. **PDU subset for v1.** Confirm scope is **Entity State PDU (PDU kind = 1)
   only**. All other DIS PDU kinds (Fire, Detonation, Collision, Start/Resume,
   etc.) are explicitly out-of-scope for v1: received, recognized by header,
   logged at `kDebug`, and dropped — never treated as `kMalformed` (they are
   valid DIS traffic the backend simply doesn't consume yet).

   **Decision (frozen):** confirmed — **Entity State PDU (pduType = 1) only**
   for v1, decoded via `dis6::EntityStatePdu::unmarshal`. Header
   `protocolVersion` values **5, 6, and 7 are accepted** (the Entity State
   base-field wire layout is identical across them for the fields we
   consume); any other version, or a datagram too short for the 12-byte PDU
   header, is `kMalformed`. Recognized-but-unsupported PDU kinds are logged
   at `kDebug` (`"ignored pdu kind=<n> from=<addr>"`) and **ignored**: they
   increment `udp_received`/`bytes_received` (via the unconditional
   `countReceived`) but neither `udp_accepted` nor either drop counter.
   Consequence, documented rather than papered over: in `mode=olv1`,
   `received == accepted + malformed + stale` holds exactly as today; in
   `mode=dis`, `received >= accepted + malformed + stale`, the remainder
   being valid-but-unconsumed DIS traffic (unsupported kinds, filtered
   exercises — see item 5). Field meanings are identical across modes; only
   this remainder differs, and `docs/PROTOCOL_DIS.md` (Phase 4) states it
   explicitly. `StateStore` gains **no new `DropKind`** — keeping the shared
   store untouched outweighs closing the accounting identity.
3. **Satellite vs. tracked-object mapping.** OLV1's wire format has a
   privileged "satellite" record plus N generic "object" records
   (`backend/include/olv/protocol.hpp`). DIS has no such distinction — every
   Entity State PDU describes one entity, uniformly. Decide the rule that
   promotes exactly one DIS entity stream to `SatelliteState`:
   - Recommended: a config field `[input.dis] satellite_entity_id =
     "site:application:entity"` (matching DIS's three-part Entity ID) that
     names the one entity treated as the satellite; every other Entity ID
     seen becomes a `SnapshotObject`, keyed the same way `UdpReceiver` keys
     objects today (by a `std::uint32_t` id — needs a deterministic 32-bit
     fold of the 3×16-bit DIS Entity ID, e.g. `site<<16 | application`
     combined with `entity` via a documented hash, since OLV's id space is
     32-bit and DIS's is 48-bit).

   **Decision (frozen):** as recommended — required config field
   `[input.dis] satellite_entity_id = "site:application:entity"` (three
   decimal `uint16`s, colon-separated; format violations are a hard config
   error). Exactly the PDU stream whose `EntityID` matches all three parts
   becomes `SatelliteState`; every other entity becomes a `SnapshotObject`.
   48→32-bit id fold, used for both satellite and objects: **FNV-1a 32-bit**
   over the six bytes `site_hi, site_lo, app_hi, app_lo, entity_hi,
   entity_lo` (big-endian per field; offset basis `2166136261`, prime
   `16777619`). FNV-1a is deterministic, spelled out in four lines of code,
   and spreads the typical "same site/app, sequential entity numbers"
   pattern across the id space better than bit-packing truncation.
   Collisions (two distinct `EntityID`s folding to one `uint32`): the
   `DisInputSource` keeps an `unordered_map<uint64 /*packed 48-bit id*/,
   uint32>` of seen entities; on a fold collision with a *different* full
   `EntityID` it logs one `kWarn` per colliding pair and lets the later
   entity overwrite — a documented limitation, not an error (probability is
   negligible at this repo's `kMaxTrackedObjects = 5000` scale).
4. **EntityType → OLV object `type`/`confidence`/`intensity` mapping.** DIS's
   `EntityType` (kind/domain/country/category/subcategory/specific/extra) has
   no OLV analogue for `confidence` or `intensity`
   (`backend/src/state_store.hpp:46-56`). Decide fixed defaults (recommended:
   `confidence = 100`, `intensity = 0.0f`) and a small lookup table from
   DIS `(kind, domain)` pairs to the existing OLV object `type` enum
   (`olv::proto::isKnownObjectType`, `backend/include/olv/protocol.hpp`) with
   an explicit fallback bucket for unmapped combinations (must not decode as
   an error — unmapped DIS entities should still render as *something*).

   **Decision (frozen):** fixed defaults `confidence = 100`,
   `intensity = 0.0f`, `flags = kFlagHasVelocity` (Entity State PDUs always
   carry `entityLinearVelocity`; `kFlagHighlight` is never set from DIS).
   `(kind, domain)` → OLV `ObjectType` table, exhaustive by construction via
   the fallback row:

   | DIS kind | DIS domain | OLV type |
   |---|---|---|
   | 1 (Platform) | 5 (Space) | `kSatellite` |
   | 1 (Platform) | 1 (Land) | `kGroundHot` |
   | 3 (Lifeform) | 1 (Land) | `kGroundHot` |
   | 2 (Munition) | any | `kComet` |
   | *anything else* | *anything else* | `kDebris` (fallback) |

   The fallback is a normal mapping outcome — no log, no drop; the entity
   renders as debris. Note the table maps the *object record's* type only;
   whether an entity is *the* satellite is decided solely by item 3's
   `satellite_entity_id` match, never by `EntityType` (a space platform that
   isn't the configured satellite is just a `kSatellite`-typed object,
   exactly like OLV1 packets can carry today).
5. **Exercise/site filtering.** DIS networks often carry multiple concurrent
   exercises on one multicast/broadcast group. Decide whether v1 needs an
   `exercise_id` allow-list filter in config (recommended: yes, optional,
   default = accept all exercise IDs) to avoid cross-exercise noise being
   treated as tracked objects.

   **Decision (frozen):** yes — optional `[input.dis] exercise_id`, a single
   value in `[0, 255]` (DIS `exerciseID` is a `uint8`; out-of-range is a
   hard config error). Unset (the default, shipped commented-out) = accept
   all exercises. A PDU whose `exerciseID` mismatches the filter is treated
   exactly like an unsupported PDU kind (item 2): `kDebug` log
   (`"filtered exercise=<n> from=<addr>"`), counted in `udp_received` only.
   A single value, not a list — one deployment watches one exercise; a list
   can be added compatibly later if ever needed.
6. **Staleness/sequencing.** OLV1 has an explicit wrapping `sequence` field
   used for stale-packet rejection (`StateStore::apply`,
   `backend/src/state_store.hpp:83`). DIS Entity State PDUs don't carry an
   equivalent sequence counter. Decide the DIS-side staleness rule
   (recommended: per-entity monotonic PDU timestamp, falling back to
   receipt-order-only if a PDU's timestamp is absent/non-monotonic — never
   reject solely because DIS lacks a sequence field).

   **Decision (frozen):** per-entity monotonic DIS timestamp with a wrap
   guard, enforced *inside* `DisInputSource` before `StateStore::apply` is
   called. Let `ts = pdu.timestamp >> 1` (the DIS timestamp's 31 MSBs are
   time-of-hour ticks; the LSB is the absolute/relative flag and is ignored
   for ordering). Per entity (keyed by full 48-bit `EntityID`):
   - first PDU, or `ts == 0`: **accept** (receipt order);
   - `ts >= last_ts`: accept, update `last_ts`;
   - `ts < last_ts` and `last_ts - ts < 2^30` (less than half the 31-bit
     range): **stale** — `countDropped(DropKind::kStale)` + `kDebug` log
     (`"dropped stale ts=<n> entity=<s:a:e>"`), mirroring the OLV1 stale
     path's counting and log shape;
   - `ts < last_ts` and `last_ts - ts >= 2^30`: hourly **wraparound** —
     accept, update `last_ts`.
   A PDU is never rejected merely for lacking sequencing information.

   **Discovered constraint (frozen consequence for §5).** Two facts about
   `StateStore::apply` (`backend/src/state_store.cpp`) shape how the DIS
   path calls it, since `StateStore` itself is untouched:
   1. `apply` **replaces the satellite wholesale on every accepted packet**.
      A DIS Entity State PDU describes one entity, so `DisInputSource` must
      cache the last-known `SatelliteState` fields and stamp them into
      *every* synthesized `StatePacket` (before the satellite entity is
      first seen, they stay zeroed — same as an OLV1 stream whose sender
      hasn't populated the satellite record). Non-satellite PDUs therefore
      carry-forward the satellite unchanged rather than zeroing it.
   2. `apply` enforces OLV1 sequence staleness. DIS staleness is already
      decided above, so `DisInputSource` synthesizes a process-local,
      strictly increasing `sequence` (starting at 1, one per accepted PDU) —
      `apply`'s check then never fires in DIS mode, and `udp_dropped_stale`
      is fed solely by the timestamp rule above. Each accepted PDU becomes
      one `apply` call: either a satellite update carrying zero object
      records, or a one-object packet (`object_total` = current live entity
      count known to `DisInputSource`).

## 4. Frozen module interface — `backend/src/input_source.hpp` (NEW, Phase 1)

```cpp
// input_source.hpp — common interface for backend intake strategies.
// Exactly one InputSource is constructed and started per process, chosen at
// boot by Config::input_mode. Both implementations own their own io_context
// and receive thread (matching UdpReceiver's existing threading model) and
// call StateStore::countReceived/countDropped/apply identically, so Stats
// mean the same thing regardless of mode.
#pragma once
namespace olv {
class InputSource {
 public:
  virtual ~InputSource() = default;
  virtual void start() = 0;  // spawns the receive thread; no-op if already started
  virtual void stop() = 0;   // stops and joins; idempotent
};
}  // namespace olv
```

`Olv1InputSource` (Phase 1) is `UdpReceiver` renamed/adapted to implement
this interface with **no other change** — same constructor shape
(`bind_address, port, StateStore&, Logger&`), same `armReceive`/
`handleDatagram` bodies, same log message formats. Existing callers/tests that
name `UdpReceiver` directly are updated to the new name only if strictly
necessary; behavior, not spelling, is the acceptance bar.

`main.cpp` (`backend/src/main.cpp:52`) changes from constructing
`olv::UdpReceiver udp(...)` directly to:

```cpp
std::unique_ptr<olv::InputSource> input = olv::makeInputSource(cfg, store, log);
input->start();
...
input->stop();
```

where `makeInputSource` (new, `input_source.cpp`) switches on
`cfg.input_mode` (`InputMode::kOlv1` default, `InputMode::kDis`) and
constructs the corresponding concrete type.

## 5. Frozen module interface — `backend/src/dis_input_source.hpp` (NEW, Phase 2)

The concrete rules this class implements are the frozen §3 decisions:
Entity State PDUs only via `dis6::EntityStatePdu` (§3.2), FNV-1a-32 id fold
and `satellite_entity_id` match (§3.3), the EntityType table with `kDebris`
fallback and fixed `confidence`/`intensity`/`flags` (§3.4), optional
`exercise_id` filter (§3.5), and the per-entity timestamp staleness rule
plus satellite carry-forward / synthesized-sequence calling convention into
`StateStore::apply` (§3.6):

```cpp
// dis_input_source.hpp — thread: DIS Entity State PDU receive loop over UDP.
// Same threading shape as Olv1InputSource: private io_context on an internal
// std::thread. Each datagram: StateStore::countReceived -> parse DIS PDU
// header -> if PDU kind == EntityState, decode via open-dis-cpp -> map to a
// StateStore-compatible update -> StateStore::apply; anything else (wrong
// magic/version-equivalent, unsupported PDU kind, malformed body) is counted
// via StateStore::countDropped(DropKind::kMalformed) and logged, exactly like
// Olv1InputSource's malformed-packet path.
#pragma once
#include "input_source.hpp"
#include "state_store.hpp"
namespace olv {
struct DisInputConfig {
  std::string bind_address = "0.0.0.0";
  std::uint16_t port = 47001;             // distinct default from OLV1's 47000
  std::optional<std::uint8_t> exercise_id;  // [0,255] filter; unset = accept all
  std::string satellite_entity_id;        // "site:application:entity", required
};
class DisInputSource : public InputSource {
 public:
  DisInputSource(const DisInputConfig&, StateStore&, Logger&);
  void start() override;
  void stop() override;
  // ... mirrors Olv1InputSource's private members (ioc_, socket_, thread_)
};
}  // namespace olv
```

## 6. Config contract — `backend/src/config.hpp`/`.cpp`, `config/backend.toml`

New keys under the existing `[input]` table, following the exact strict-schema
precedent of the `[network]`/`[broadcast]`/`[state]`/`[logging]` sections. The
first-party TOML subset (`olv/toml.hpp`, mirrored in `frontend/js/toml.js`) has
single-level tables only, so the DIS group is realized as flat `dis_*` keys
rather than a nested `[input.dis]` table (see the §7 Phase 2 reconciliation):

```toml
[input]
# Intake strategy selected at boot. "olv1" = existing first-party UDP binary
# protocol (default, unchanged). "dis" = IEEE 1278.1 DIS Entity State PDUs.
mode = "olv1"          # "olv1" | "dis"

# Consulted only when mode = "dis" (still schema-checked whenever present):
dis_bind = "0.0.0.0"
dis_port = 47001
# dis_exercise_id = 1   # optional filter, 0-255; commented out = accept all
# dis_satellite_entity_id = "1:1:1"  # site:application:entity — required when mode = "dis"
```

Validation is strict, matching every existing section: `mode` outside
`{"olv1","dis"}`, `dis_exercise_id` outside `[0,255]`, `dis_port` outside
`[1,65535]`, or a `dis_satellite_entity_id` that isn't three colon-separated
decimal `uint16`s is a hard error. The `dis_*` keys are only *consulted* under
`mode = "dis"`, but are still schema-checked whenever present (unknown keys/bad
types are errors regardless of mode). One cross-field rule: `mode = "dis"`
with no `dis_satellite_entity_id` is a hard error (it has no sensible default),
checked on the fully merged config in `parseArgs`.

`Config` gains `InputMode input_mode = InputMode::kOlv1;` and a
`DisInputConfig dis;` member. `--input-mode olv1|dis` CLI flag added to
`parseArgs`; there are no `--dis-*` flags (the frozen contract adds only
`--input-mode`), so a DIS run supplies `dis_satellite_entity_id` via
`--config`. The shared `parseDisEntityId` helper (declared in
`dis_input_source.hpp`) is the single source of truth for the id format, used by
both config validation and `DisInputSource`.

## 7. Phased implementation plan

**Phase 0 — Research & decisions.** ✅ Done (2026-07-17). §3 items answered
in place and folded into §4-§6 as the frozen contract. No code changes.
Gate: architect sign-off before Phase 1 starts.

**Phase 1 — Input abstraction refactor (behavior-preserving).** ✅ Done
(2026-07-17; signed off). Note: `mode = "dis"` parses per the frozen §6
schema but `makeInputSource` throws "input mode 'dis' is not implemented
yet" until Phase 2 ships; the startup log line gains an `input_mode=` field
only when the mode is non-default, keeping default-run logs byte-identical.
- Add `backend/src/input_source.hpp` (§4).
- Adapt `UdpReceiver` → `Olv1InputSource` implementing `InputSource`
  (rename in place; no logic changes).
- Add `Config::input_mode` (default `kOlv1`) + `--input-mode` flag; update
  `config/backend.toml` with the `[input] mode = "olv1"` default (documented
  as a no-op line, matching every other field's "default shown" convention).
- `main.cpp` constructs via a small factory instead of naming the concrete
  class directly (§4).
- **Acceptance:** full `ctest` suite green, existing integration test
  unchanged and passing, `--input-mode` absent from the command line behaves
  identically to before this phase (regression guard — diff logs/wire output
  against a pre-refactor run).

**Phase 2 — DIS ingestion.** ✅ Done (2026-07-17; signed off). Two Phase 0
reconciliations against the actual `open-dis-cpp` v1.2.0 tree and the
first-party TOML parser, both consistent with the decisions' intent:
1. **utils nesting.** §3.1's pruning rule named "`src/dis6/` and `src/utils/`",
   but upstream nests utils at `src/dis6/utils/` (there is no top-level
   `src/utils/`). Vendoring `src/dis6/` recursively captures it — same result.
2. **generated export header.** `<dis6/opendis6_export.h>` is produced by
   CMake's `generate_export_header` upstream, not a source file. We generate it
   the same way into the build tree (`generate_export_header(open_dis_cpp
   BASE_NAME OPENDIS6 ...)`); for our STATIC target the macros expand to
   nothing. Nothing hand-authored is added to the pruned tree, honoring the
   no-local-changes policy.
3. **`[input.dis]` → flat `dis_*` keys (§6 correction).** The first-party TOML
   subset (`olv/toml.hpp`, mirrored in `frontend/js/toml.js`) supports
   single-level tables only, so a nested `[input.dis]` table is unparseable.
   The same fields ship as flat `dis_bind` / `dis_port` / `dis_exercise_id` /
   `dis_satellite_entity_id` keys under the existing `[input]` table — identical
   strict validation, zero parser change, C++/JS subsets kept in sync. §6 below
   reflects this.

Delivered:
- Vendored `open-dis-cpp` under `third_party/open-dis-cpp/` (`LICENSE` +
  `src/dis6/`, incl. `utils/`); `backend/CMakeLists.txt` compiles it as a
  static `open_dis_cpp` library (SYSTEM includes, `-w`), linked only via
  `olv_core`'s DIS path — `olv_sim` is untouched.
- `DisInputSource` (`backend/src/dis_input_source.{hpp,cpp}`) implementing the
  frozen §3 mapping/fold/staleness/filtering rules, wired into
  `makeInputSource`. Its `processDatagram()` is a public seam so tests drive the
  full translation without a live socket (mirroring how the OLV1 path is tested
  via `proto::decode`).
- `DisInputConfig` + flat `dis_*` schema in `config.{hpp,cpp}` with strict
  validation (bad `satellite_entity_id` format, out-of-range port/exercise are
  errors), plus a cross-field check that `mode = "dis"` requires
  `dis_satellite_entity_id`.
- `backend/tests/test_dis_input_source.cpp` (hand-built Entity State PDU byte
  arrays) covering satellite recognition, object mapping, the EntityType table
  + `kDebris` fallback, unsupported-kind drop, exercise filtering, malformed
  rejection (short header / truncated body / bad version), and per-entity
  timestamp staleness (older/zero/wraparound/per-entity/synthesized-sequence);
  plus `[input] dis_*` config tests in `test_config.cpp`.
- `THIRD_PARTY.md` row + `scripts/gen_sbom.py` component (regenerated
  `sbom/backend.cdx.json`).
- **Acceptance met:** full `ctest` green (backend_unit 109 tests, integration,
  sim_unit); new code warning-clean under `-DOLV_WERROR=ON`; a default
  (`mode=olv1`) run's startup log stays byte-identical (no `input_mode=` field);
  a live smoke test (`--input-mode dis`, hand-built PDUs → `olv_ws_probe`)
  renders the satellite and a mapped object in the WS JSON.

**Phase 3 — Simulator DIS emission (optional, recommended for testability).**
✅ Done (2026-07-17). Delivered:
- `[send] protocol = "olv1" | "dis"` + `--protocol` CLI flag in
  `sim_config.{hpp,cpp}`, with DIS-only settings `[send] dis_exercise_id`
  (default 1), `dis_site` (default 1), and `dis_satellite_entity_id`
  (default `"1:1:1"`, validated by the shared `olv::parseDisEntityId`, which
  moved from `dis_input_source.hpp` to `backend/include/olv/dis_entity_id.hpp`
  so the simulator reuses the identical parser — still one source of truth).
  No `--dis-*` flags, matching the backend's `--input-mode`-only precedent.
  When `protocol = "dis"` and no port was set by file or `--port`, the
  destination port defaults to 47001 (the backend's DIS default) so both
  ends' defaults line up.
- `simulator/src/dis_builder.{hpp,cpp}`: one Entity State PDU per entity
  (satellite first, under the configured satellite EntityID; objects spread
  their 32-bit OLV id as `application = id >> 16`, `entity = id & 0xFFFF`
  with `site = dis_site`), encoded via the same open-dis-cpp library the
  backend decodes with. EntityType is the exact inverse of the frozen §3.4
  table; DIS timestamps derive from the cycle counter (not `frame.t`), so
  they stay monotone across CSV `--loop` wraps and the backend's §3.6
  staleness rule accepts every PDU, hourly wrap included. The `chunk`
  setting is OLV1-only (DIS has no multi-record packet shape).
- Tests: `simulator/tests/test_dis_builder.cpp` (7 cases, decoding emitted
  buffers with open-dis-cpp itself) + 8 new `[send]`-schema/CLI cases in
  `test_sim_config.cpp`; `config/simulator.toml` documents the new keys.
- **Acceptance met** with the documented DIS-inherent losses (already frozen
  in §3.4): live smoke run `olv_sim --generate 20 --protocol dis` →
  `olv_backend --input-mode dis` → `olv_ws_probe` shows all PDUs accepted
  (received == accepted, zero drops), the satellite recognized, and all 20
  objects present with mapped types and velocities. Object *ids* are the
  backend's deterministic FNV-1a folds of the emitted EntityIDs (not the raw
  OLV ids), per-object confidence/intensity are fixed at 100/0.0, and STAR
  objects render as debris (no §3.4 row) — identical rendering therefore
  means "same positions/velocities/types for every mapped category", not a
  byte-identical WS stream.

**Phase 4 — Docs, SBOM, and repo bookkeeping.**
- New `docs/PROTOCOL_DIS.md`: supported PDU subset, the EntityType→OLV type
  mapping table, satellite-entity rule, staleness rule (mirrors the structure
  of `docs/PROTOCOL_UDP.md`).
- Update `docs/PLAN.md`'s architecture section to describe intake as
  pluggable (`InputSource`) rather than naming `UdpReceiver` exclusively.
- Update `THIRD_PARTY.md` (new `open-dis-cpp` row: role, license, vendoring
  note) and regenerate `sbom/backend.cdx.json` via `scripts/gen_sbom.py`
  (extend the script to emit the new component).
- Update README prerequisites/config examples; update
  `config/backend.toml`'s committed example with the new `[input]` section.

**Phase 5 — Verification.**
- Full `cmake --build build && ctest --test-dir build`.
- `node --test "frontend/tests/*.test.mjs"` (sanity — frontend is untouched
  but confirms no accidental cross-contamination).
- Manual smoke test: run `olv_backend --input-mode dis` against a small
  script emitting a few hand-built Entity State PDUs; confirm the frontend
  (unchanged) renders the resulting objects exactly as it would for an
  equivalent OLV1 packet.
- Explicit default-path regression check: run `olv_backend` with **no**
  `--input-mode`/`[input]` config at all and confirm byte-identical logging
  and `StateStore` behavior versus a pre-feature build.

## 8. Acceptance criteria (overall)

1. Default behavior (`mode=olv1`, i.e. no new config) is unchanged —
   verified by the existing test suite plus the Phase 1/5 regression checks.
2. `[input] mode = "dis"` (or `--input-mode dis`) makes `olv_backend` ingest
   DIS Entity State PDUs and populate `StateStore`/the WS wire format
   identically in shape to the OLV1 path — the frontend requires zero
   changes to render either.
3. Both modes share identical `Stats` semantics (received/accepted/
   dropped/rate) and identical log message shapes (mode-specific fields only
   in the DIS-path detail, not a different logging scheme).
4. Exactly one new runtime third-party dependency (`open-dis-cpp`) is
   introduced, deliberately reviewed in Phase 0, and fully recorded in
   `THIRD_PARTY.md` + `sbom/backend.cdx.json` — no silent/undocumented
   dependency addition.
5. Unsupported DIS PDU kinds and unmapped `EntityType`s degrade gracefully
   (dropped/defaulted, logged) — never crash, never silently corrupt
   `StateStore`.
6. `ctest` and `node --test` remain fully green throughout every phase.
