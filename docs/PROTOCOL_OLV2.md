# OLV2 UDP Wire Protocol — Version 1 (normative)

Binary datagram format for the backend's `olv2` input mode
(`olv_backend --input-mode olv2`), emitted by `olv_sim --protocol olv2`.
Each datagram describes **one target track** with up to 25 time-stamped
update points; every point also carries the satellite (ownship) state at
that instant. The reference implementation is
[`include/olv/protocol_olv2.hpp`](../include/olv/protocol_olv2.hpp); if this
document and the header disagree, the header wins and this document must be
fixed. Design rationale: [docs/features/FEATURE_OLV2.md](features/FEATURE_OLV2.md).

OLV2 is independent of OLV1 ([PROTOCOL_UDP.md](PROTOCOL_UDP.md)): a
different magic, its own version counter, and its own listen port. A backend
runs exactly one input mode at a time.

## 1. General rules

- Transport: UDP. One message per datagram. No fragmentation/reassembly at the
  application layer.
- **Byte order: little-endian** for every multi-byte field.
- **Alignment: none.** The layout is defined by byte offsets below. Encoders
  and decoders MUST serialize field-by-field (no C struct memory overlay).
- Floating point: IEEE 754 `binary64` (`f64`) and `binary32` (`f32`).
- Coordinates: ECEF meters. Velocities: ECEF meters/second.
- **Time: transmitted per point** as `f64` UTC seconds since the Unix epoch
  (1970-01-01T00:00:00Z), fractional seconds allowed, stamped by the sender.
  It orders points and drives staleness; the backend still stamps its own
  receive time for `lastDataTime` and object expiry.
- **The satellite is the ownship.** A deployment has exactly one satellite;
  every datagram, for every track, carries that same satellite.
- **Batches are disjoint.** Each datagram carries only points newer than
  every point previously sent for that track. A sender MUST NOT resend
  points (no sliding window).

## 2. Limits

| Constant | Value |
|---|---|
| `kMaxPoints` | 25 |
| `kHeaderSize` | 24 bytes |
| `kPointSize` | 84 bytes |
| `kCrcSize` | 4 bytes |
| `kMinPacketSize` = 24 + 84 + 4 | **112 bytes** |
| `kMaxPacketSize` = 24 + 25×84 + 4 | **2128 bytes** |
| `kMaxCoordinateMeters` (validation bound, each axis) | 1×10¹³ m |

A datagram with more than 17 points exceeds a 1500-byte MTU and will be
IP-fragmented; this is acceptable on localhost/LAN (the deployment scope,
same as OLV1's 6204-byte maximum). Senders MAY use fewer points per datagram
(`olv_sim` `[send] olv2_points`) to stay under one MTU.

**Batch period vs. expiry.** The backend expires a track not refreshed
within `[state] expiry_seconds` (default 15 s) of *receive* time. A sender's
batch period — points per datagram ÷ sample rate — MUST stay below that, or
tracks disappear between datagrams (25 points at 1 Hz = one datagram per
25 s).

## 3. Packet layout — `TRACK_UPDATE` (msg_type = 1)

```
offset size type  field
------ ---- ----  -----------------------------------------------------------
0      4    u8[4] magic          'O','L','V','2'  (0x4F 0x4C 0x56 0x32)
4      1    u8    version        1
5      1    u8    msg_type       1 = TRACK_UPDATE
6      2    u16   hdr_flags      reserved, MUST be 0
8      4    u32   sequence       increments by 1 per DATAGRAM sent (wraps);
                                 diagnostic only, NOT used for staleness
12     4    u32   track_id       target identifier
16     4    u32   sat_id         satellite (ownship) identifier
20     1    u8    target_type    object type enum (PROTOCOL_UDP.md §3)
21     1    u8    target_flags   bit1 HIGHLIGHT: source requests emphasis
                                 all other bits reserved, send 0
22     1    u8    confidence     0–100 (%); values >100 are clamped by RX
23     1    u8    point_count    N, 1–25
24     84×N       points         see below; strictly ascending t
24+84N 4    u32   crc32          CRC-32 (IEEE 802.3, reflected, poly
                                 0x04C11DB7, init 0xFFFFFFFF, final XOR
                                 0xFFFFFFFF — i.e. zlib crc32) computed
                                 over bytes [0, 24+84N)
```

Total datagram length MUST equal `24 + 84*point_count + 4` exactly.

### Point (84 bytes)

```
offset size type   field
------ ---- ----   ----------------------------------------------------------
+0     8    f64    t              UTC seconds since the Unix epoch
+8     1    u8     point_flags    bit0 SAT_HAS_VEL: sat_vel fields are valid
                                  bit1 TGT_HAS_VEL: tgt_vel fields are valid
                                  bits 2–7 reserved, send 0
+9     3    u8[3]  reserved       send 0
+12    8    f64    sat_pos_x      ECEF meters
+20    8    f64    sat_pos_y
+28    8    f64    sat_pos_z
+36    8    f64    tgt_pos_x      ECEF meters
+44    8    f64    tgt_pos_y
+52    8    f64    tgt_pos_z
+60    4    f32    sat_vel_x      ECEF m/s; send 0 when SAT_HAS_VEL unset
+64    4    f32    sat_vel_y
+68    4    f32    sat_vel_z
+72    4    f32    tgt_vel_x      ECEF m/s; send 0 when TGT_HAS_VEL unset
+76    4    f32    tgt_vel_y
+80    4    f32    tgt_vel_z
```

### Target type

`target_type` uses the OLV1 object type enum (0 `UNKNOWN`, 1 `DEBRIS`,
2 `STAR`, 3 `COMET`, 4 `SATELLITE`, 5 `GROUND_HOT`). Any other value is not
an error: the receiver decodes it as `UNKNOWN` (0). OLV2 has no intensity
field, so ground-hot temperature and star magnitude are not representable;
the backend reports intensity 0 for every OLV2 target.

## 4. Receiver validation (normative)

A receiver MUST drop (never partially apply) a datagram when, checked in
order:

1. length < 112 (`kTooShort`) or > 2128 (`kTooLong`)
2. magic ≠ `OLV2` (`kBadMagic`)
3. version ≠ 1 (`kBadVersion`)
4. msg_type ≠ 1 (`kBadMsgType`)
5. point_count = 0 or > 25 (`kBadPointCount`)
6. length ≠ 24 + 84·point_count + 4 (`kBadLength`)
7. CRC mismatch (`kBadCrc`)
8. any f32/f64 field, including `t`, is NaN or ±Inf (`kNonFinite`)
9. any position component with |value| > 1e13 m (`kOutOfRange`)
10. any `t` ≤ 0, or the points' `t` values are not strictly ascending
    (`kBadTime`)

Items 8 and 9 are evaluated point by point (point 0's item 8, then its
item 9, then point 1's …); item 10 is evaluated only after every point has
passed 8 and 9.

Unrecognized `target_type`, reserved `target_flags` bits, and reserved
`point_flags` bits do not drop the datagram; they decode as `UNKNOWN` / 0.

and additionally, at the input-source layer:

11. **Stale track batch:** with `last[track_id]` = the newest `t` accepted
    for that track, drop the datagram iff `points[0].t ≤ last[track_id]`.
    The first datagram for a track is always accepted. A conforming sender
    (disjoint batches, §1) never trips this; it catches duplicated and
    reordered datagrams and non-conforming sliding-window senders. Counted
    as stale.

Every drop is counted per reason (`udpDropped` in the WebSocket stats =
malformed + stale) and logged with the source address and reason.

## 5. Backend mapping (informative)

For each accepted datagram the backend:

- **Target** — upserts object `track_id` from the **newest** point: position,
  velocity (`HAS_VELOCITY` iff that point has TGT_HAS_VEL), `target_type`,
  `confidence`, HIGHLIGHT from `target_flags`, intensity 0. It appears as a
  normal row in the WebSocket `objects` array.
- **Satellite** — takes the newest point's satellite position as the primary
  satellite (`id` = `sat_id`, `seq` = `sequence`), but only if that point's
  `t` is newer than the satellite sample already held: datagrams for
  different tracks interleave, and an older batch must not move the
  satellite backwards. Velocity is the point's `sat_vel` when SAT_HAS_VEL is
  set; otherwise it is estimated from the last two points' positions
  (N ≥ 2), else 0 (the sat-view camera orients on it). A change of `sat_id`
  is logged at warn and applied.
- **Trail** — forwards every point's `(t, tgt_pos)` to clients once, as
  WebSocket `trailPoints` rows ([PROTOCOL_WS.md](PROTOCOL_WS.md) §2).

## 6. Configuration

| Component | Setting | Default |
|---|---|---|
| backend | `[input] mode = "olv2"` / `--input-mode olv2` | `olv1` |
| backend | `[input] olv2_bind` | `"0.0.0.0"` |
| backend | `[input] olv2_port` | `47002` |
| simulator | `[send] protocol = "olv2"` / `--protocol olv2` | `olv1` |
| simulator | `[send] olv2_points` (points per datagram, 1–25) | `10` |

With `protocol = "olv2"` and no explicit port, the simulator sends to 47002.

## 7. Versioning

`version` bumps on any layout change. Receivers reject unknown versions. New
message types may be added under the same version; receivers reject unknown
`msg_type` values.
