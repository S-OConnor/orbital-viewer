# WebSocket JSON Protocol — Version 1 (normative)

Messages sent by `olv_backend` to browser clients over WebSocket (text
frames, UTF-8 JSON, one JSON object per frame). Clients never send messages;
anything received from a client is ignored. Default endpoint:
`ws://<host>:8765/`.

The backend serializer is `include/olv/json_writer.hpp` + `src/json_writer.cpp`; the frontend
parser is `frontend/js/net.js` (`parseStateMessage`). The integration test
re-parses captured backend frames with the real frontend parser.

## 1. `hello` — sent once, immediately after the WebSocket handshake

```json
{
  "type": "hello",
  "protocolVersion": 1,
  "serverTime": "2026-07-01T12:34:56.789Z",
  "broadcastHz": 1.0,
  "limits": { "maxObjects": 5000 }
}
```

## 2. `state` — broadcast to all clients at `broadcastHz` (default 1 Hz)

```json
{
  "type": "state",
  "serverTime": "2026-07-01T12:34:56.789Z",
  "lastDataTime": "2026-07-01T12:34:56.500Z",
  "satellite": { "id": 1, "seq": 42,
                 "pos": [4126540.2, -4681712.5, 1003432.1],
                 "vel": [1234.56, 2345.67, -6543.21] },
  "objects": [
    [1001, 1, 6923371.4, 12000.0, -55000.2, 7611.0, 0.0, 0.0, 87, 0.0, 0],
    [2001, 5, 1113194.9, -4842330.0, 3985029.2, null, null, null, 95, 1450.0, 1]
  ],
  "stats": { "udpReceived": 480, "udpAccepted": 478, "udpDropped": 2,
             "udpRateHz": 8.0, "wsClients": 1, "objectCount": 2,
             "broadcastSeq": 12 }
}
```

### Field semantics

| Field | Type | Meaning |
|---|---|---|
| `serverTime` | ISO-8601 UTC, ms | backend wall clock at serialization |
| `lastDataTime` | ISO-8601 UTC, ms · or `null` | receive time of the last **accepted** UDP packet; `null` until first data. Displayed as "latest data time". |
| `satellite` | object · or `null` | primary satellite state; `null` until first data. `seq` = sequence of the packet that provided it. `pos` m ECEF, `vel` m/s ECEF. |
| `objects` | array of 11-element rows | see below; rows are objects not yet expired (refreshed within the backend expiry window, default 15 s) |
| `stats.udpReceived/udpAccepted/udpDropped` | u64 counters | totals since backend start; dropped = malformed (any decode failure, including bad CRC) + stale |
| `stats.udpRateHz` | number | accepted packets/s over the trailing 5 s window |
| `stats.wsClients` | int | currently connected clients |
| `stats.objectCount` | int | rows in `objects` |
| `stats.broadcastSeq` | u64 | increments per broadcast tick |

### Object row columns (fixed order, 11 columns)

| # | Name | Type | Notes |
|---|---|---|---|
| 0 | `id` | u32 | |
| 1 | `cat` | int 0–5 | 0 unknown · 1 debris · 2 star · 3 comet · 4 satellite · 5 groundHot; clients render any other integer as unknown |
| 2–4 | `px, py, pz` | number | ECEF meters, rounded to 0.1 m |
| 5–7 | `vx, vy, vz` | number **or `null`** | ECEF m/s rounded to 0.01; all three `null` when the source set no HAS_VELOCITY flag |
| 8 | `conf` | int 0–100 | confidence % |
| 9 | `intensity` | number | Kelvin (groundHot), magnitude (star), else 0 |
| 10 | `flags` | int | bit1 (value 2) = HIGHLIGHT; bit0 mirrors has-velocity |

Row-array encoding is used instead of key/value objects to roughly halve
message size at 5,000 objects (~450 KB vs ~800 KB per broadcast).

## 3. Client behavior (informative)

- Reconnect with backoff (frontend uses 1 s doubling to 10 s max).
- Treat unknown `type` values as ignorable (forward compatibility).
- `parseStateMessage` MUST reject: non-JSON, missing/unknown `type`, missing
  `objects` array on `state`, rows not of length 11, non-numeric id/pos,
  non-integer cat. An integer cat outside 0–5 parses as `unknown`.
- Client-side staleness: if no `state` frame arrives for >3 s, show the
  connection as stalled (the backend broadcasts every second even with no
  UDP data, so silence means a transport problem).
