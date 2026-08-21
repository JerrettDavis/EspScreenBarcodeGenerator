# USB Serial Protocol 1.0

This is the default protocol; the device boots into it on every reset. For the opt-in binary EspLink v2 protocol (COBS hop frames, negotiated via an `upgrade` request sent over this v1 line protocol), see [`docs/PROTOCOL_V2.md`](PROTOCOL_V2.md).

## Transport

- Physical connection: board USB connector through its USB-to-UART bridge.
- Serial settings: 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.
- Encoding: UTF-8.
- Framing: one compact JSON object followed by `\n`.
- Carriage returns are ignored.
- Maximum inbound line length: 4,096 bytes before the newline.
- Requests and responses are independent JSON objects. No binary bytes are placed directly on the line.

On startup the device emits an unsolicited event:

```json
{"event":"ready","device":"EspScreenBarcodeGenerator","protocol":"1.0","firmware":"0.1.0"}
```

## Correlation and errors

A request may include any JSON scalar as `id`. The firmware copies it into every response associated with that request. Host automation should always supply an ID and ignore unrelated ready/events.

Success:

```json
{"id":12,"ok":true,"cmd":"close","message":"barcode closed"}
```

Failure:

```json
{
  "id":12,
  "ok":false,
  "cmd":"generate",
  "error":{"code":"generation_failed","message":"EAN-13 check digit is invalid"}
}
```

Protocol-level parse errors may omit `id` because the request could not be parsed.

## Commands

### `hello` / `ping`

Request:

```json
{"id":1,"cmd":"hello"}
```

Response includes device, firmware, protocol, transport, and screen dimensions.

### `capabilities`

```json
{"id":2,"cmd":"capabilities"}
```

Returns advertised symbologies, commands, payload/line/matrix limits, transfer encoding, raw-matrix support, touch UI, and preset support. Hosts should use this rather than assuming every firmware revision supports the same formats.

### `status`

```json
{"id":3,"cmd":"status"}
```

Returns visibility/current-state flags, human-readable status, free heap, and current matrix metadata when present.

### `generate`

Generate a symbol on-device.

```json
{
  "id":4,
  "cmd":"generate",
  "type":"qr",
  "data":"LAB-TEST-001",
  "display":true,
  "options":{
    "ecc":"M",
    "rotation":"auto",
    "quiet":-1,
    "min_module":2,
    "invert":false,
    "qr_min_version":1,
    "qr_max_version":20
  }
}
```

Options may be at the top level or inside `options`. A top-level value wins when both are present.

| Field | Type | Default/current | Range/meaning |
|---|---|---|---|
| `type` | string | active type | `qr`, `datamatrix`, `aztec`, `code128`, `gs1-128`, `code39`, `upca`, `ean13`, `ean8`, `itf`, `itf14`, `codabar`, `msi` |
| `data` | string | active payload | UTF-8 JSON string; embedded NUL is not representable through this field |
| `data_base64` | string | none | Arbitrary payload bytes; takes precedence over `data` |
| `display` | boolean | `true` | Render immediately after generation |
| `save_as` | string | none | Save the generated specification under a preset name |
| `ecc` | string | active | `L`, `M`, `Q`, `H`; used by QR |
| `rotation` | string/int | active | `auto`, `0`, `90`, `180`, `270` |
| `quiet` | integer | `-1` | `-1` means symbology default; explicit range 0-32 |
| `min_module` | integer | 2 | Minimum physical pixels per module, 1-8 |
| `rect` | boolean | false | Request rectangular Data Matrix |
| `invert` | boolean | false | White modules on black background |
| `checksum` | boolean | true | MSI Mod 10; retail formats always calculate/validate required digits |
| `qr_min_version` | integer | 1 | 1-40 |
| `qr_max_version` | integer | 20 | 1-40 and not below minimum |
| `aztec_security` | integer | 23 | 1-90 percent |
| `aztec_layers` | integer | 1 | 0-32; 0 requests an Aztec Rune for numeric 0-255 input |

Example GS1-128:

```json
{"id":5,"cmd":"generate","type":"gs1-128","data":"0109501101530003{FNC1}10ABC","display":true}
```

Accepted FNC1/group-separator markers for Code 128/GS1-128 payloads are `{FNC1}`, `<FNC1>`, `<GS>`, and an actual ASCII 0x1D byte supplied through `data_base64`.

A successful response returns normalized data and matrix dimensions:

```json
{
  "id":4,
  "ok":true,
  "cmd":"generate",
  "type":"qr",
  "width":29,
  "height":29,
  "linear":false,
  "quiet":4,
  "displayed":true,
  "normalized_data":"LAB-TEST-001"
}
```

### `display`

Redisplay the current symbol:

```json
{"id":6,"cmd":"display"}
```

Load, regenerate, and display a preset:

```json
{"id":7,"cmd":"display","name":"LOT_SAMPLE"}
```

### `close`

```json
{"id":8,"cmd":"close"}
```

Returns to the home UI and retains the current symbol.

### `home`

```json
{"id":9,"cmd":"home"}
```

Forces the home UI even when a non-barcode subview is open.

### `save`

```json
{"id":10,"cmd":"save","name":"LOT_SAMPLE"}
```

Names must be 1-24 characters from `A-Z`, `a-z`, `0-9`, `-`, and `_`. Raw uploaded matrices are not preset records in 0.1.0.

### `load`

```json
{"id":11,"cmd":"load","name":"LOT_SAMPLE","display":false}
```

Loads and regenerates the preset. Set `display` to true to show it immediately.

### `delete`

```json
{"id":12,"cmd":"delete","name":"LOT_SAMPLE"}
```

### `list`

```json
{"id":13,"cmd":"list"}
```

Response:

```json
{"id":13,"ok":true,"cmd":"list","presets":["LOT_SAMPLE","SLOT01"]}
```

### `backlight`

```json
{"id":14,"cmd":"backlight","on":false}
```

The device remains active. Turning the backlight off does not close or clear the current symbol.

### `reboot`

```json
{"id":15,"cmd":"reboot"}
```

The firmware sends its acknowledgement, flushes serial output, waits briefly, and restarts.

## Raw matrix upload

Raw upload is the compatibility path for PDF417, externally generated symbols, golden fixtures, and intentionally malformed test cases.

### Packing contract

- Matrix dimensions describe logical barcode modules, not TFT pixels.
- Bits are continuous row-major order: `(0,0)`, `(1,0)`, through the row, then the next row.
- The first module is bit 7 of byte 0, then bit 6, and so on.
- Rows are not padded to byte boundaries.
- Unused low bits in the last byte should be zero.
- `linear=true` requires `height=1`; the firmware supplies bar height during rendering.
- CRC is standard IEEE CRC-32 with polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`, matching Python `binascii.crc32`.

For a 3x2 matrix:

```text
1 0 1
0 1 0
```

The packed bit stream is `10101000`, or one byte `0xA8`.

### `upload_begin`

```json
{
  "id":20,
  "cmd":"upload_begin",
  "width":3,
  "height":2,
  "linear":false,
  "quiet":4,
  "rotation":"auto",
  "invert":false,
  "label":"external-pdf417",
  "display":true
}
```

The response reports `bytes_expected` and `next_offset`.

### `upload_chunk`

```json
{"id":21,"cmd":"upload_chunk","offset":0,"data":"qA=="}
```

Chunks must be sequential. The recommended size is 384 decoded bytes; 48-768 is supported by the host tool. The serial line limit still applies.

### `upload_end`

```json
{"id":22,"cmd":"upload_end","crc32":3240191126}
```

The firmware verifies the final byte count and optional CRC, installs the current matrix, and displays it when requested.

### `upload_abort`

```json
{"id":23,"cmd":"upload_abort"}
```

Discards transfer state. Starting a second upload while one is active is rejected until the first is ended or aborted.

## Matrix download

Request:

```json
{"id":30,"cmd":"download","chunk_bytes":384}
```

The device emits multiple responses with the same `id`:

1. `download_begin` with dimensions, display metadata, byte count, encoding, and CRC.
2. Zero or more `download_chunk` events with `offset` and Base64 `data`.
3. `download_end` with final byte count and CRC.

Example begin:

```json
{
  "id":30,
  "ok":true,
  "cmd":"download",
  "event":"download_begin",
  "width":29,
  "height":29,
  "linear":false,
  "quiet":4,
  "rotation":"auto",
  "invert":false,
  "label":"QR Code",
  "bytes":106,
  "encoding":"base64-packed-msb-first",
  "crc32":123456789
}
```

Hosts must validate offsets, total length, and CRC before trusting the matrix. `tools/espbarcode.py` performs these checks and can write the transfer JSON schema or a PBM file.

## Matrix JSON file schema

The host utility persists transfers as:

```json
{
  "schema":"esp-screen-barcode-matrix/1",
  "width":29,
  "height":29,
  "linear":false,
  "quiet":4,
  "rotation":"auto",
  "invert":false,
  "label":"QR Code",
  "encoding":"base64-packed-msb-first",
  "crc32":123456789,
  "data":"..."
}
```

The JSON file represents a module matrix, not a rendered 320x480 screenshot.

## Common error codes

| Code | Meaning |
|---|---|
| `invalid_json` | Line was not valid JSON |
| `invalid_request` | Top-level JSON value was not an object |
| `line_too_long` | Request exceeded 4,096 bytes |
| `missing_command` | `cmd` absent/empty |
| `unknown_command` | Command not supported |
| `invalid_spec` | Invalid symbology, payload field, or option |
| `generation_failed` | Encoder or fit/render validation failed |
| `display_failed` | No current symbol, preset failure, or symbol cannot fit |
| `missing_name` / `invalid_name` | Preset name absent or wrong type |
| `save_failed`, `load_failed`, `delete_failed` | LittleFS preset operation failed |
| `raw_not_persisted` | Attempted to save an uploaded raw matrix as a preset |
| `upload_active` | A previous upload is still open |
| `invalid_dimensions` / `matrix_too_large` | Raw matrix exceeds bounds |
| `no_upload` | Chunk/end issued without `upload_begin` |
| `missing_offset` | Chunk offset absent or not unsigned |
| `unexpected_offset` | Chunk was not the next sequential range |
| `invalid_base64` | Chunk data could not be decoded |
| `overflow` | Chunk exceeds declared matrix size |
| `incomplete` | `upload_end` before all bytes arrived |
| `crc_mismatch` | Final CRC does not match |
| `no_symbol` | Download requested without a current matrix |
