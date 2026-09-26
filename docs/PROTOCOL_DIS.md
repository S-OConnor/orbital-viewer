# DIS Input Protocol — IEEE 1278.1 Entity State PDUs (normative)

How `olv_backend --input-mode dis` ingests IEEE 1278.1 Distributed
Interactive Simulation (DIS) traffic and translates it into the same
`StateStore` contract the OLV1 path fills (docs/PROTOCOL_UDP.md). The
reference implementation is
[`src/dis_input_source.cpp`](../src/dis_input_source.cpp);
if this document and the code disagree, the code wins and this document must
be fixed. Design rationale and the frozen decision record live in
[`docs/features/FEATURE_INPUT_SOURCES.md`](features/FEATURE_INPUT_SOURCES.md) §3.

DIS itself is an external standard — this document does not restate IEEE
1278.1. It specifies exactly which subset this backend consumes, how DIS
fields map onto OLV concepts, and how everything else is counted and logged.

## 1. General rules

- Transport: UDP, one PDU per datagram (the standard DIS bundling of several
  PDUs per datagram is **not** supported; trailing bytes after the first
  decoded Entity State PDU are ignored).
- **Byte order: big-endian** (network order) for every multi-byte field, per
  IEEE 1278.1 — note this is the opposite of OLV1's little-endian layout.
- Decoding is done with the third-party `open-dis-cpp` library (v1.2.0,
  BSD-2-Clause, see THIRD_PARTY.md), DIS protocol version 6 class set.
- Coordinates: DIS world coordinates are geocentric (ECEF) meters and
  velocities ECEF m/s — the same frame OLV uses, so positions and velocities
  pass through numerically unchanged.
- Time: PDU timestamps are used **only** for per-entity staleness ordering
  (§6). The backend still stamps its own receive time as `lastDataTime`,
  exactly as in OLV1 mode.

## 2. Accepted PDU subset

| Header field | Accepted values | Anything else |
|---|---|---|
| `protocolVersion` (byte 0) | 5, 6, or 7 | dropped as malformed |
| `exerciseID` (byte 1) | all, or the configured `dis_exercise_id` | ignored (received-only, §7) |
| `pduType` (byte 2) | 1 = Entity State | ignored (received-only, §7) |

Versions 5–7 share an identical wire layout for every Entity State base
field this backend consumes, which is why all three are accepted; the PDU is
always decoded with the DIS6 classes. A datagram shorter than the 12-byte
PDU header, or one whose body fails to decode as an Entity State PDU
(truncated body, bogus articulation-parameter count), is dropped as
malformed (§7).

Consumed Entity State fields: `EntityID` (site, application, entity),
`EntityType` (kind, domain), `entityLocation` (3×f64 ECEF m),
`entityLinearVelocity` (3×f32 ECEF m/s), and the header `timestamp`.
All other fields (orientation, dead reckoning, markings, appearance,
articulation parameters) are decoded but unused.

## 3. Satellite promotion rule

DIS has no privileged satellite record — every Entity State PDU describes
one entity, uniformly. The backend requires a config field naming the one
entity treated as the primary satellite:

```toml
[input]
mode = "dis"
dis_satellite_entity_id = "1:1:1"   # site:application:entity
```

The format is three colon-separated decimal `uint16`s (parser:
[`include/olv/dis_entity_id.hpp`](../include/olv/dis_entity_id.hpp));
a format violation, or `mode = "dis"` without this key, is a hard config
error at startup. Exactly the PDU stream whose `EntityID` matches all three
parts becomes the satellite state; **every** other entity becomes a tracked
object. The satellite decision never looks at `EntityType` — a space
platform that isn't the configured satellite is just a satellite-*typed*
object, exactly as OLV1 packets can carry today.

Because a DIS PDU updates one entity at a time while the OLV state layer
replaces the satellite wholesale on every accepted packet, the DIS path
carries the last-known satellite state forward into every update. Before
the satellite entity is first seen, its fields are zero — the same as an
OLV1 stream whose sender hasn't populated the satellite record.

## 4. Entity ID mapping (48-bit DIS → 32-bit OLV)

OLV object ids are `u32`; a DIS `EntityID` is three `u16`s (48 bits). Both
the satellite id and every object id are produced by the same deterministic
fold — **FNV-1a 32-bit** over the six bytes

```
site_hi, site_lo, app_hi, app_lo, entity_hi, entity_lo   (big-endian per field)
offset basis 2166136261, prime 16777619
```

Collisions (two distinct `EntityID`s folding to the same `u32`) are a
documented limitation, not an error: the backend logs one `warn`-level
`fold collision` line per colliding pair and lets the later entity
overwrite the earlier one. At this repo's `kMaxTrackedObjects = 5000`
scale the probability is negligible.

## 5. EntityType → OLV object mapping

The DIS `EntityType` `(kind, domain)` pair selects the OLV object type
(enum values per docs/PROTOCOL_UDP.md §3); the table is exhaustive by
construction via the fallback row — an unmapped combination is a normal
outcome (no log, no drop), rendered as unknown:

| DIS kind | DIS domain | OLV type |
|---|---|---|
| 1 (Platform) | 5 (Space) | `SATELLITE` (4) |
| 1 (Platform) | 1 (Land) | `GROUND_HOT` (5) |
| 3 (Lifeform) | 1 (Land) | `GROUND_HOT` (5) |
| 2 (Munition) | *any* | `COMET` (3) |
| 0 (Other) | 5 (Space) | `DEBRIS` (1) |
| *anything else* | *anything else* | `UNKNOWN` (0) — fallback |

DIS has no analogue for OLV's per-object `confidence`/`intensity`/`flags`,
so they are fixed: `confidence = 100`, `intensity = 0.0`, `flags =
HAS_VELOCITY` (Entity State PDUs always carry a linear velocity;
`HIGHLIGHT` is never set from DIS). Note there is no `(kind, domain)` row
producing `STAR` — DIS traffic cannot express OLV star markers.

## 6. Staleness rule (per-entity DIS timestamp)

OLV1 uses a per-packet sequence number; DIS Entity State PDUs carry none.
Instead, staleness is enforced per entity (keyed by the full 48-bit
`EntityID`) using the PDU header timestamp. Let `ts = timestamp >> 1` (the
31 MSBs are time-of-hour ticks; the LSB is the absolute/relative-time flag
and is ignored for ordering). Then:

1. First PDU from an entity, or `ts == 0`: **accept** (receipt order).
2. `ts >= last_ts`: **accept**, update `last_ts`.
3. `ts < last_ts` and `last_ts − ts < 2³⁰` (less than half the 31-bit
   range): **stale** — dropped, counted, logged at debug
   (`dropped stale ts=<n> entity=<s:a:e>`).
4. `ts < last_ts` and `last_ts − ts ≥ 2³⁰`: hourly **wraparound** —
   **accept**, update `last_ts`.

A PDU is never rejected merely for lacking sequencing information. The OLV1
sequence check in the shared state layer is satisfied by a synthesized,
strictly increasing per-process sequence (one per accepted PDU), so the
stale counter in DIS mode is fed **solely** by the rule above.

## 7. Receiver validation & stats accounting (normative)

Every datagram increments `udpReceived`/bytes. Then, checked in order:

| # | Condition | Outcome |
|---|---|---|
| 1 | length < 12 (PDU header) | drop **malformed** — `warn: dropped too_short` |
| 2 | `protocolVersion` ∉ {5, 6, 7} | drop **malformed** — `warn: dropped bad_version` |
| 3 | `exerciseID` ≠ configured filter (when set) | **ignored** — `debug: filtered exercise=<n>`, received-only |
| 4 | `pduType` ≠ 1 (Entity State) | **ignored** — `debug: ignored pdu kind=<n>`, received-only |
| 5 | Entity State body fails to decode | drop **malformed** — `warn: dropped malformed_body` |
| 6 | per-entity timestamp stale (§6) | drop **stale** — `debug: dropped stale` |
| 7 | otherwise | **accepted** — applied to the state store |

"Ignored" rows 3–4 are valid DIS traffic the backend simply doesn't consume:
they count in `udpReceived` only — neither accepted nor either drop counter.
Consequently the stats identity differs between modes, by design:

- `mode = olv1`: `received == accepted + malformed + stale` (exact, as today);
- `mode = dis`: `received >= accepted + malformed + stale` — the remainder
  is unsupported PDU kinds plus filtered exercises.

Field meanings (`udpReceived`/`udpAccepted`/`udpDropped`/rate) are otherwise
identical across modes; no new drop kinds were added.

## 8. Configuration reference

Flat `dis_*` keys under `[input]` in `config/backend.toml` (the first-party
TOML subset has single-level tables only), consulted only when
`mode = "dis"` but schema-checked whenever present:

```toml
[input]
mode = "dis"                        # select DIS intake at boot ("olv1" default)
dis_bind = "0.0.0.0"                # UDP listen address
dis_port = 47001                    # UDP listen port (OLV1's default is 47000)
# dis_exercise_id = 1               # optional filter, 0-255; unset = accept all
dis_satellite_entity_id = "1:1:1"   # required in DIS mode (§3)
```

The only DIS-related CLI flag is `--input-mode olv1|dis`; the `dis_*` values
come from the config file. Precedence and strictness match every other
config section (defaults < `--config` file < flags; unknown keys/bad
types/out-of-range are startup errors).

## 9. Sender notes (`olv_sim --protocol dis`)

The simulator can emit the same subset (`[send] protocol = "dis"`, see
`config/simulator.toml` and
[`tools/simulator/src/dis_builder.hpp`](../tools/simulator/src/dis_builder.hpp)): one
Entity State PDU per entity per cycle, satellite first under the configured
`dis_satellite_entity_id`, each object's 32-bit OLV id spread losslessly as
`application = id >> 16`, `entity = id & 0xFFFF` with a fixed configured
`site`. Round-tripping through `olv_backend --input-mode dis` reproduces
the same positions, velocities, and mapped types as the equivalent OLV1
run, with the DIS-inherent losses documented above: object ids become the
backend's FNV-1a folds (deterministic but not the raw OLV ids), confidence
and intensity are fixed at 100 / 0.0, and `STAR` objects degrade to
`UNKNOWN` (§5). The simulator emits debris as `(0, 5)` and star/unknown
objects as `(0, 0)`.
