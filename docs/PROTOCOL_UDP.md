# OLV1 UDP Wire Protocol — Version 1 (normative)

Binary datagram format sent by data sources (here: `olv_sim`) to `olv_backend`.
The reference implementation is [`include/olv/protocol.hpp`](../include/olv/protocol.hpp);
if this document and the header disagree, the header wins and this document
must be fixed.

## 1. General rules

- Transport: UDP. One message per datagram. No fragmentation/reassembly at the
  application layer.
- **Byte order: little-endian** for every multi-byte field.
- **Alignment: none.** The layout is defined by byte offsets below. Encoders
  and decoders MUST serialize field-by-field (no C struct memory overlay).
- Floating point: IEEE 754 `binary64` (`f64`) and `binary32` (`f32`).
- Coordinates: ECEF meters. Velocities: ECEF meters/second.
- Time: **not transmitted.** Receivers treat every accepted packet as "now"
  and stamp their own receive time (requirement: all data arrives at current
  time). Staleness is handled with the sequence number only.

## 2. Limits

| Constant | Value |
|---|---|
| `kMaxObjectsPerPacket` | 128 |
| `kMaxTrackedObjects` (per update cycle, `object_total`) | 5000 |
| `kHeaderSize` | 56 bytes |
| `kRecordSize` | 48 bytes |
| `kCrcSize` | 4 bytes |
| `kMaxPacketSize` = 56 + 128×48 + 4 | **6204 bytes** |
| `kMaxCoordinateMeters` (validation bound, each axis) | 1×10¹³ m |

A 6204-byte datagram exceeds a 1500-byte MTU and will be IP-fragmented; this
is acceptable on localhost/LAN (the deployment scope). Senders MAY use smaller
chunks (`olv_sim --chunk N`) to stay under one MTU.

An update cycle with more than 128 objects is split across multiple packets.
Each packet is **self-contained**: it repeats the satellite state and carries
`object_total` for the whole cycle. Receivers merge object records into a
table keyed by object ID and expire entries not refreshed within an expiry
window (backend default 15 s). Packet loss therefore degrades gracefully.

## 3. Packet layout — `STATE_UPDATE` (msg_type = 1)

```
offset size type  field
------ ---- ----  -----------------------------------------------------------
0      4    u8[4] magic            'O','L','V','1'  (0x4F 0x4C 0x56 0x31)
4      1    u8    version          1
5      1    u8    msg_type         1 = STATE_UPDATE
6      2    u16   hdr_flags        reserved, MUST be 0
8      4    u32   sequence         increments by 1 per PACKET sent (wraps)
12     4    u32   sat_id           primary satellite identifier
16     8    f64   sat_pos_x        ECEF meters
24     8    f64   sat_pos_y
32     8    f64   sat_pos_z
40     4    f32   sat_vel_x        ECEF m/s
44     4    f32   sat_vel_y
48     4    f32   sat_vel_z
52     2    u16   object_count     records in THIS packet, ≤ 128
54     2    u16   object_total     objects in this update cycle, ≤ 5000
56     48×N       object records   (see below)
56+48N 4    u32   crc32            CRC-32 (IEEE 802.3, reflected, poly
                                   0x04C11DB7, init 0xFFFFFFFF, final XOR
                                   0xFFFFFFFF — i.e. zlib crc32) computed
                                   over bytes [0, 56+48N)
```

Total datagram length MUST equal `56 + 48*object_count + 4` exactly.

### Object record (48 bytes)

```
offset size type field
------ ---- ---- ------------------------------------------------------------
+0     4    u32  object_id
+4     1    u8   object_type      see enum below
+5     1    u8   flags            bit0 HAS_VELOCITY: vel fields are valid
                                  bit1 HIGHLIGHT: source requests emphasis
                                  bits 2–7 reserved, send 0
+6     1    u8   confidence       0–100 (%); values >100 are clamped by RX
+7     1    u8   reserved         send 0
+8     8    f64  pos_x            ECEF meters
+16    8    f64  pos_y
+24    8    f64  pos_z
+32    4    f32  vel_x            ECEF m/s; send 0 when HAS_VELOCITY unset
+36    4    f32  vel_y
+40    4    f32  vel_z
+44    4    f32  intensity        GROUND_HOT: temperature in Kelvin
                                  STAR: apparent magnitude
                                  others: 0
```

### Object type enum

| Value | Name | Notes |
|---|---|---|
| 1 | `DEBRIS` | orbital debris |
| 2 | `STAR` | distant direction marker; position = direction × large radius (e.g. 1e12 m). Fixed in ECEF for the demo (Earth-rotation drift intentionally ignored — documented simplification). |
| 3 | `COMET` | |
| 4 | `SATELLITE` | tracked satellite other than the primary |
| 5 | `GROUND_HOT` | hot object on Earth's surface; stationary in ECEF |

Any other value ⇒ the packet is rejected (`kBadObjectType`).

## 4. Receiver validation (normative)

A receiver MUST drop (never partially apply) a datagram when, checked in
order:

1. length < 60 (`kTooShort`) or > 6204 (`kTooLong`)
2. magic ≠ `OLV1` (`kBadMagic`)
3. version ≠ 1 (`kBadVersion`)
4. msg_type ≠ 1 (`kBadMsgType`)
5. object_count > 128 or object_total > 5000 or object_count > object_total
   (`kTooManyObjects`)
6. length ≠ 56 + 48·object_count + 4 (`kBadLength`)
7. CRC mismatch (`kBadCrc`)
8. any f32/f64 field is NaN or ±Inf (`kNonFinite`)
9. any position component with |value| > 1e13 m (`kOutOfRange`)
10. unknown object_type (`kBadObjectType`)

and additionally, at the state layer:

11. **Stale sequence:** with `last` = last accepted sequence, accept iff
    `(int32_t)(sequence - last) > 0` (wraparound-safe); the first packet after
    startup is always accepted. Duplicates and reordered-older packets are
    dropped and counted as stale.

Every drop is counted per reason and logged with source address and reason.

## 5. Versioning

`version` bumps on any layout change. Receivers reject unknown versions. New
message types may be added under the same version; receivers reject unknown
`msg_type` values.
