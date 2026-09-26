# Feature: OLV2 Input Source — Per-Track Batched Time-Series Updates

Status: **Phase 0 — decisions frozen, awaiting architect sign-off to start
Phase 1.** §3–§7 are the **frozen contract**; implementation agents MUST NOT
change them without architect sign-off. Companion to
docs/features/FEATURE_INPUT_SOURCES.md (the pluggable `InputSource` seam this
feature plugs into), docs/PROTOCOL_UDP.md (OLV1, **unchanged**), and
docs/PROTOCOL_WS.md (receives one additive, optional field).

## 1. Overview

OLV2 is a third boot-selectable intake mode (`[input] mode = "olv2"` /
`--input-mode olv2`) alongside OLV1 and DIS. Its shape differs from both:

| | OLV1 | DIS | **OLV2** |
|---|---|---|---|
| Datagram carries | 1 satellite + ≤128 objects, one instant | 1 entity, one instant | **1 track: satellite + target, 1–25 instants** |
| Time on the wire | none (arrival = now) | DIS time-of-hour | **UTC epoch seconds per point** |
| Staleness | global packet sequence | per-entity timestamp | **per-track sample time** |

Each OLV2 datagram describes exactly one target track and carries up to 25
**new** update points for it (disjoint batches — never a sliding window).
Every point carries the satellite position alongside the target position.

## 2. Decisions (user-answered 2026-09-26)

| # | Question | Decision |
|---|---|---|
| D1 | Wire format / name | Architect's proposed layout accepted "for now"; magic and name **`OLV2`**. |
| D2 | Ownship / satellite | The satellite in every message is **always the same one, and it is our ownship.** There is no separate ownship entity: OLV2's satellite block maps onto the existing primary satellite (`SatelliteState`, WS `satellite`, sat-view camera). **Consequence:** the originally proposed separate ownship block is removed from the wire format; no new entity kind reaches the store, the WS protocol, or the frontend. |
| D3 | Batch semantics | **New update points only** — consecutive datagrams for one track never overlap. A datagram whose first point is not newer than the track's newest accepted point is a duplicate/reorder and is dropped whole as stale (§4.4). |
| D4 | Time base | Absolute **UTC seconds since the Unix epoch** (f64, sub-second fraction allowed), stamped by the sender. `lastDataTime` in the WS protocol keeps its existing meaning: backend **receive** time of the last accepted datagram. |

Architect defaults (not asked; reversible, flagged here for review):

* **A1 — use all points, not just the newest.** The newest point is the
  track's current state; *every* point is forwarded to clients as trail data
  (§6), because with disjoint batches anything else discards up to 24/25 of
  the data.
* **A2 — no intensity on the wire.** The accepted layout has no intensity
  field, so OLV2 targets carry `intensity = 0` (ground-hot temperature / star
  magnitude are not representable) — a documented loss, like DIS. Adding a
  header `f32 intensity` later is a version bump.
* **A3 — missing satellite velocity is estimated.** If a datagram's points do
  not set SAT_HAS_VEL, the backend finite-differences the message's satellite
  samples (N ≥ 2) so the sat-view camera (which orients on velocity) keeps
  working; with N = 1 velocity is 0.

## 3. Wire format — `OLV2` version 1 (frozen)

General rules identical to OLV1 (PROTOCOL_UDP.md §1): one message per UDP
datagram, **little-endian**, no alignment (serialize field-by-field), IEEE 754,
ECEF meters and m/s, CRC-32 (zlib) trailer.

### 3.1 Limits

| Constant | Value |
|---|---|
| `kMaxPoints` | 25 |
| `kHeaderSize` | 24 bytes |
| `kPointSize` | 84 bytes |
| `kCrcSize` | 4 bytes |
| `kMinPacketSize` = 24 + 84 + 4 | 112 bytes |
| `kMaxPacketSize` = 24 + 25×84 + 4 | **2128 bytes** |
| `kMaxCoordinateMeters` | 1×10¹³ m (same as OLV1) |

A full 25-point datagram exceeds a 1500-byte MTU and is IP-fragmented —
acceptable in the LAN/localhost deployment scope, same precedent as OLV1's
6204-byte maximum. Senders wanting single-frame datagrams use ≤ 17 points.

### 3.2 Layout — `TRACK_UPDATE` (msg_type = 1)

```
offset size type  field
------ ---- ----  -----------------------------------------------------------
0      4    u8[4] magic          'O','L','V','2'  (0x4F 0x4C 0x56 0x32)
4      1    u8    version        1
5      1    u8    msg_type       1 = TRACK_UPDATE
6      2    u16   hdr_flags      reserved, MUST be 0
8      4    u32   sequence       per-datagram sender counter (diagnostic only;
                                 becomes WS satellite.seq; NOT used for staleness)
12     4    u32   track_id       target id (becomes the WS object id)
16     4    u32   sat_id         satellite (ownship) id
20     1    u8    target_type    OLV1 ObjectType enum; unknown values -> 0
21     1    u8    target_flags   bit1 HIGHLIGHT; all other bits reserved, send 0
22     1    u8    confidence     0-100 (%); >100 clamped by RX
23     1    u8    point_count    N, 1..25
24     84×N       points         strictly ascending t (see below)
24+84N 4    u32   crc32          zlib CRC-32 over bytes [0, 24+84N)
```

Point (84 bytes):

```
+0     8    f64  t               UTC seconds since Unix epoch
+8     1    u8   point_flags     bit0 SAT_HAS_VEL, bit1 TGT_HAS_VEL; rest 0
+9     3    u8[3] reserved       send 0
+12    24   3×f64 sat_pos        ECEF m
+36    24   3×f64 tgt_pos        ECEF m
+60    12   3×f32 sat_vel        ECEF m/s; send 0 when SAT_HAS_VEL unset
+72    12   3×f32 tgt_vel        ECEF m/s; send 0 when TGT_HAS_VEL unset
```

### 3.3 Receiver validation (drop whole datagram, checked in order)

1. length < 112 (`kTooShort`) or > 2128 (`kTooLong`)
2. magic ≠ `OLV2` (`kBadMagic`)
3. version ≠ 1 (`kBadVersion`)
4. msg_type ≠ 1 (`kBadMsgType`)
5. point_count == 0 or > 25 (`kBadPointCount`)
6. length ≠ 24 + 84·point_count + 4 (`kBadLength`)
7. CRC mismatch (`kBadCrc`)
8. any f32/f64 field NaN/±Inf, including `t` (`kNonFinite`)
9. any position component |v| > 1e13 m (`kOutOfRange`)
10. `t` ≤ 0, or points not strictly ascending in `t` (`kBadTime`)

Unrecognized `target_type` decodes as UNKNOWN (not an error), as in OLV1.

### 3.4 Stateful rule (in `Olv2InputSource`, §5)

11. **Per-track staleness:** with `last[track_id]` = newest accepted `t` for
    that track, the datagram is **stale iff `points[0].t ≤ last[track_id]`**
    — dropped whole, counted in `udp_dropped_stale`. First datagram for a
    track is always accepted. (Because of D3 a correct sender never trips
    this; it catches duplicates, reordering, and sliding-window senders.)

## 4. Mapping into the existing model

| OLV2 | Store / WS |
|---|---|
| newest point's `tgt_pos`/`tgt_vel`, `track_id`, `target_type`, `confidence`, `target_flags` | object row `track_id` (same 11-column row as OLV1; `flags` bit0 = newest point's TGT_HAS_VEL, bit1 = HIGHLIGHT; intensity 0) |
| satellite sample with the newest `t` seen **across all tracks** | `satellite` (`id` = `sat_id`, `seq` = that datagram's `sequence`). Replaced only when the datagram's newest `t` > the stored satellite sample time, since datagrams for different tracks interleave. An `sat_id` change is logged once at warn (D2 says it never happens) and applied. |
| every point's `t` + `tgt_pos` | `trailPoints` (§6) |
| receive time | `lastDataTime`, object expiry (unchanged semantics) |

**Operational note — expiry vs batch period.** Objects expire when not
refreshed within `[state] expiry_seconds` (default 15 s) of *receive* time. A
sender batching 25 points at 1 Hz refreshes each track only every 25 s, so
tracks would blink out. Batch period (points ÷ sample rate) MUST be below
the backend expiry; the simulator warns when its own batch period exceeds
10 s.

## 5. Frozen interfaces

### 5.1 `include/olv/protocol_olv2.hpp` (NEW, header-only)

Namespace `olv::proto::olv2`. Includes `olv/protocol.hpp` and reuses
`proto::crc32`, `proto::detail::*` byte helpers, `ObjectType`,
`normalizeObjectType`, `kMaxCoordinateMeters`. **`protocol.hpp` is not
modified.**

```cpp
namespace olv::proto::olv2 {
inline constexpr std::array<std::uint8_t, 4> kMagic{'O', 'L', 'V', '2'};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 24, kPointSize = 84, kCrcSize = 4;
inline constexpr std::uint8_t kMaxPoints = 25;
inline constexpr std::size_t kMinPacketSize = kHeaderSize + kPointSize + kCrcSize;               // 112
inline constexpr std::size_t kMaxPacketSize = kHeaderSize + kMaxPoints * kPointSize + kCrcSize;  // 2128
enum class MsgType : std::uint8_t { kTrackUpdate = 1 };
inline constexpr std::uint8_t kPointSatHasVel = 0x01, kPointTgtHasVel = 0x02;

struct Point {
  double t = 0.0;                          // UTC epoch seconds
  std::uint8_t flags = 0;                  // kPoint* bits
  double sat_px = 0, sat_py = 0, sat_pz = 0;
  double tgt_px = 0, tgt_py = 0, tgt_pz = 0;
  float sat_vx = 0, sat_vy = 0, sat_vz = 0;
  float tgt_vx = 0, tgt_vy = 0, tgt_vz = 0;
};
struct TrackPacket {
  std::uint32_t sequence = 0, track_id = 0, sat_id = 0;
  std::uint8_t target_type = 0, target_flags = 0, confidence = 0;
  std::vector<Point> points;               // 1..kMaxPoints, strictly ascending t
};
enum class DecodeError { kNone = 0, kTooShort, kTooLong, kBadMagic, kBadVersion, kBadMsgType,
                         kBadPointCount, kBadLength, kBadCrc, kNonFinite, kOutOfRange, kBadTime };
const char* toString(DecodeError e);       // snake_case, matching OLV1's strings where shared
std::vector<std::uint8_t> encode(const TrackPacket& pkt);  // truncates to 25 points, clamps confidence
DecodeError decode(const std::uint8_t* data, std::size_t len, TrackPacket& out);  // §3.3 items 1-10
}
```

### 5.2 `include/olv/state_store.hpp` (additive only)

Existing `apply(const proto::StatePacket&, …)` and its OLV1 global-sequence
staleness are **untouched**; OLV1 and DIS never call the new method.

```cpp
struct TrailPoint {                 // one target sample, forwarded to clients
  std::uint32_t id = 0;
  double t = 0.0;                   // UTC epoch seconds
  double px = 0.0, py = 0.0, pz = 0.0;
};

struct TrackUpdate {                // store-neutral; the OLV2 source translates into this
  std::uint32_t sequence = 0;       // -> SatelliteState::seq
  SatelliteState satellite;         // newest satellite sample (vel already estimated if needed)
  double satellite_t = 0.0;         // its sample time, for newest-wins across tracks
  SnapshotObject target;            // newest target sample, as an object row
  std::vector<TrailPoint> trail;    // every target sample, ascending t
};

struct Snapshot {                   // + one field
  // ...existing fields...
  std::vector<TrailPoint> trail_points;  // drained: points applied since the previous snapshot()
};

class StateStore {
 public:
  // Counts one accepted datagram (udp_accepted, rate window, lastDataTime),
  // upserts `target`, replaces the satellite iff satellite_t > stored sample
  // time, appends `trail` to the pending buffer. No staleness check here —
  // the caller (Olv2InputSource) owns §3.4.
  void applyTrack(const TrackUpdate& u, std::chrono::system_clock::time_point wall,
                  std::chrono::steady_clock::time_point mono);
  // snapshot(): additionally MOVES the pending trail buffer into
  // Snapshot::trail_points (WsServer's broadcast tick is the only caller).
  static constexpr std::size_t kMaxPendingTrailPoints = 125000;  // 5000 tracks x 25; oldest dropped
};
```

### 5.3 `include/olv/input_source.hpp` / `olv2_input_source.hpp` (NEW)

* `InputMode::kOlv2`; `parseInputMode("olv2")`; `toString` → `"olv2"`;
  `makeInputSource` case.
* `Olv2InputSource` — same shape as `DisInputSource`: binds in the
  constructor (throws on failure), private io_context + thread,
  `start()/stop()`, public `processDatagram(data, len, from)` test seam,
  buffer `kMaxPacketSize + 1` so oversize datagrams surface as `kTooLong`.
  Owns `std::unordered_map<std::uint32_t, double> last_t_` (§3.4) and the
  A3 velocity estimate. Logging mirrors OLV1: accepted at debug
  (`track=.. points=.. seq=.. from=..`), malformed at warn with the reason,
  stale at debug.

```cpp
struct Olv2InputConfig {
  std::string bind_address = "0.0.0.0";
  std::uint16_t port = 47002;       // distinct from OLV1 47000 / DIS 47001
};
```

### 5.4 Config contract

Backend `[input]` gains flat keys (single-level TOML subset, DIS precedent):
`olv2_bind = "0.0.0.0"`, `olv2_port = 47002` (consulted only when
`mode = "olv2"`, schema-checked whenever present). CLI: only
`--input-mode olv2` (no `--olv2-*` flags, matching DIS). Startup log gains
`input_mode=olv2` (already non-default-only).

Simulator `[send]`: `protocol = "olv1" | "dis" | "olv2"` (`--protocol olv2`);
`olv2_points = 10` (`[1, 25]`, points per datagram). When
`protocol = "olv2"` and no port was set by file or `--port`, `dest_port`
defaults to 47002 (existing `dest_port_set` mechanism).

## 6. WebSocket protocol — additive `trailPoints` (frozen)

`protocolVersion` stays **1**: `parseState` in `frontend/js/net.js` already
ignores unrecognized top-level keys, so old clients are unaffected.

```json
"trailPoints": [[2001, 1790000000.125, 6923371.4, 12000.0, -55000.2], ...]
```

* Rows `[id, t, px, py, pz]` — `t` UTC epoch seconds `%.3f`, positions `%.1f`.
* Contains the target samples applied **since the previous broadcast** (a
  delta, not a history), so bandwidth scales with the incoming point rate,
  not with history length. A client that connects mid-stream builds trails
  from that point on — identical to today's client-built trails.
* **Key omitted when empty** — so OLV1 and DIS state frames stay
  byte-identical to pre-feature output (regression guard, §8).
* Parser: absent → `[]`; present → must be an array of 5-element all-finite
  numeric rows, else the message is rejected (same strictness as `objects`).

Frontend consumption (renderer): for ids that have received `trailPoints`,
the trail is fed **only** from those points (the 1 Hz snapshot append is
skipped for them, avoiding duplicates). Sample time maps onto the renderer's
trail clock via the existing serverTime-based scene clock
(`age = serverTime − t`). Server-fed trails use their own cap of 512 samples
(`TRAIL_CAP` = 64 would truncate a 10 Hz track to 6.4 s of a 30 s trail).
Satellite trail stays client-built (1 Hz is ample for an orbit). No new UI
panels, settings, or atlas icons (D2 removed the ownship).

## 7. Simulator emission

`tools/simulator/src/olv2_builder.{hpp,cpp}` (pure std, in `olv_sim_lib`):

```cpp
class Olv2Batcher {
 public:
  explicit Olv2Batcher(std::size_t points_per_datagram);   // clamped [1, 25]
  // Records one frame at UTC epoch time t_epoch (must increase across calls).
  void push(const Frame& frame, double t_epoch);
  bool ready() const;                                      // points_per_datagram frames buffered
  // One datagram per object id seen in the buffered frames (objects missing
  // from some frames just carry fewer points); satellite sample taken from
  // each frame. Clears the buffer. `seq` incremented per datagram.
  std::vector<std::vector<std::uint8_t>> flush(std::uint32_t& seq);
};
```

`t_epoch` = run-start wall clock + `cycles / rate_hz` (cycle counter, not
`frame.t`, so CSV `--loop` stays monotone — DIS precedent). `main.cpp`
pushes every frame and sends on `ready()`; a partial batch is flushed at
normal end of run. Documented losses: intensity (A2); object ids pass
through unchanged (unlike DIS).

## 8. Phased plan & ownership

No two agents own the same file. Architect writes all frozen headers and
protocol docs.

**Phase 1 — Freeze (architect).** `protocol_olv2.hpp` (full, it's the spec);
`docs/PROTOCOL_OLV2.md` (normative, PROTOCOL_UDP.md structure);
`state_store.hpp` + `olv2_input_source.hpp` + `input_source.hpp` declarations;
`docs/PROTOCOL_WS.md` §2 `trailPoints`; CMake entries + empty test files
(`test/test_protocol_olv2.cpp`, `test/test_olv2_input_source.cpp`,
`tools/simulator/tests/test_olv2_builder.cpp`) so Phase 2 agents never share
a CMakeLists.

**Phase 2 — Parallel implementation.**

| Agent | Model | Owns |
|---|---|---|
| A — store/WS | Sonnet | `src/state_store.cpp`, `src/json_writer.cpp`, `test/test_state_store.cpp`, `test/test_json_writer.cpp`, `test/test_protocol_olv2.cpp` |
| B — intake | Sonnet | `src/olv2_input_source.cpp`, `src/input_source.cpp`, `include/olv/config.hpp`, `src/config.cpp`, `config/backend.toml`, `test/test_olv2_input_source.cpp`, `test/test_config.cpp` |
| C — simulator | Sonnet | `tools/simulator/src/{olv2_builder.*,sim_config.*,main.cpp}`, `config/simulator.toml`, `tools/simulator/tests/{test_olv2_builder,test_sim_config}.cpp` |
| D — frontend | Sonnet | `frontend/js/{net,model,renderer}.js`, `frontend/tests/{net_parse,model}.test.mjs`, `frontend/tests/validate_message.mjs` |

**Phase 3 — Integration review (Opus).** Cross-check against §3–§7, style,
`-DOLV_WERROR=ON` clean. Tooling: `tools/ws_probe.cpp` prints `trailPoints`
count; `scripts/integration_test.sh` gains an OLV2 leg;
`containers/compose.yaml` exposes `47002/udp`; `scripts/run_all.sh`
passthrough.

**Phase 4 — Docs (Sonnet).** README (feature bullet, run example, config
rows), docs site (new `reference/protocol-olv2.html`, nav in
`assets/docs.js`, CLI/config pages, regenerated search index),
`THIRD_PARTY.md`/`sbom/*.cdx.json` unchanged (no new dependency).

**Phase 5 — Verification.** `ctest` + `node --test` green; OLV2 smoke
(`olv_sim --generate 50 --protocol olv2 --rate 10` → `olv_backend
--input-mode olv2` → `olv_ws_probe`: received == accepted, satellite + 50
objects, frames pass `validate_message.mjs`, `trailPoints` ≈ 10 per track per
second); DIS smoke unchanged; **OLV1 regression**: a recorded OLV1 stream
replayed to the pre-feature and post-feature backends yields byte-identical
logs (modulo timestamps/ephemeral ports) and byte-identical WS state frames
(modulo `serverTime`); browser check of trails via `frontend/dev/smoke.html`.

## 9. Acceptance criteria

1. OLV1 and DIS behavior unchanged — wire, logs, WS frames (§8 Phase 5).
2. `--input-mode olv2` ingests OLV2 per §3; every §3.3/§3.4 drop reason has
   a unit test; `Stats` keep identical meaning across all three modes.
3. The satellite (ownship) from OLV2 drives the existing satellite panel and
   sat-view camera with no frontend change beyond trails.
4. All points of every accepted datagram reach clients as `trailPoints`
   exactly once per broadcast; trails render at source sample rate.
5. No new third-party dependency; `ctest` and `node --test` green every phase.
