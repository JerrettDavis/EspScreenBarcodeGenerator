# Architecture

## Goals

`EspScreenBarcodeGenerator` turns the Hosyond ESP32 screen into a deterministic lab instrument rather than a desktop image viewer. The device owns barcode generation, layout, display state, touch interaction, and persistent presets. A host sends intent and parameters, not pre-rendered screenshots, unless it deliberately uses raw matrix upload for an unsupported symbology or exact fixture.

## Component model

```text
                     USB cable
                         |
                 USB-to-UART bridge
                         |
          newline-delimited JSON protocol
                         |
+------------------------v-------------------------+
|                    ESP32 firmware                |
|                                                  |
|  +----------------+     +---------------------+  |
|  | UsbProtocol    |---->| BarcodeApplication  |  |
|  | IDs, errors,   |     | State + workflows   |  |
|  | chunks, CRC32  |     +----+-----------+----+  |
|  +----------------+          |           |       |
|                              |           |       |
|                    +---------v--+   +----v-----+ |
|                    | Core       |   | Presets | |
|                    | encoders + |   | LittleFS| |
|                    | layout     |   +----------+ |
|                    +---------+--+                |
|                              |                   |
|                    integer module rectangles     |
|                              |                   |
|                    +---------v----------+        |
|                    | TFT_eSPI / ST7796U |        |
|                    +--------------------+        |
|                         ^                        |
|                         | XPT2046 touch          |
|                 direct touch UI/keyboard         |
+--------------------------------------------------+
```

## Portable barcode core

`lib/EspBarcodeCore` is standard C++17 and has no Arduino, display, filesystem, or serial dependency. It provides:

- `BarcodeSpec`: symbology and generation options.
- `BarcodeResult`: normalized data, packed module matrix, default quiet zone, and linear/matrix classification.
- `BitMatrix`: continuous row-major, MSB-first packed bits with a 512x512 bound.
- Encoders for the supported linear and matrix symbologies.
- Base64 helpers shared by firmware and native tests.
- `calculateLayout`: selects an integer module size and valid rotation for a fixed screen.

QR is adapted through the external `ricmoo/QRCode` C library. If its header is absent in a plain host build, the rest of the core still compiles and returns an explicit QR dependency error. PlatformIO resolves and tests the real QR adapter.

## Application state

`BarcodeApplication` owns the active specification and current rendered result. The current symbol can be:

1. Generated from a `BarcodeSpec`.
2. Loaded and regenerated from a preset.
3. Installed as a raw packed matrix uploaded over USB.

Closing a barcode returns to the home UI without discarding the current symbol, so the host can redisplay or download it. A generated symbol retains its specification; a raw matrix retains only matrix/display metadata and is not stored as a preset in v0.1.0.

## Rendering pipeline

1. Encoder returns logical black/white modules without quiet-zone pixels.
2. Application chooses the quiet zone from the explicit option or symbology default.
3. `calculateLayout` evaluates portrait and 90-degree layouts for `auto` rotation.
4. It computes `floor(screen_dimension / logical_modules)` and requires that integer scale to meet `minModulePixels`.
5. The TFT is filled with the background color.
6. Each black module is drawn as an exact `scale x scale` rectangle. Linear bars are drawn as tall rectangles from a one-row matrix.

No resized bitmap is pushed to the screen. This avoids filtering artifacts and keeps bar/module edges aligned to the physical pixel grid.

## USB transport

The ESP32-WROOM-32E does not expose native USB device functionality. The board's connector reaches the ESP32 through a USB-to-UART bridge, so the PoC exposes an ordinary COM/tty port at 115200 baud.

The application protocol is deliberately independent of UART framing:

- UTF-8 JSON object per line.
- Optional caller-supplied `id`, echoed in responses.
- Stable command names and error objects.
- Chunked binary transfer represented as Base64.
- Declared dimensions, sequential offsets, exact byte counts, and CRC32.

A later Windows service, virtual device driver, WebSerial client, TCP bridge, or BLE adapter can map the same request model to a different transport.

## Persistence

LittleFS uses the final 960 KiB of the custom 4 MiB partition table. Each preset is a JSON file under `/presets`. Names are restricted to 1-24 alphanumeric, dash, or underscore characters. The UI automatically selects `SLOT01` through `SLOT32`; USB callers may use meaningful names.

The mount currently permits format-on-failure to make first boot self-initializing. A production qualification should verify power-loss behavior and decide whether formatting on an unexpected mount failure is acceptable for the lab process.

## Error strategy

Errors are explicit and do not silently degrade scan quality:

- Invalid payload or check digit: generation fails.
- Unknown symbology or malformed option: protocol returns `invalid_spec`.
- Symbol too dense for the display/minimum module size: display fails.
- Overlong serial line: discarded through the next newline and reported.
- Matrix upload with wrong dimensions, order, byte count, offset, Base64, or CRC: rejected.
- Missing current symbol or preset: rejected without altering prior state.

## Resource bounds

| Resource | Bound |
|---|---:|
| Input payload | 2,048 bytes |
| Matrix dimensions | 1-512 modules per axis |
| Matrix storage | 32,768 packed bytes |
| Serial request line | 4,096 bytes |
| Presets | 32 |
| Preset name | 24 characters |
| Upload chunk recommendation | 384 bytes |
| Upload/download chunk accepted | 48-768 bytes |

A 320x480 display normally imposes a much smaller practical module count than the storage bound. The larger raw-matrix bound exists for transfer/inspection but display still requires a valid integer layout.

## Extension points

- Add an encoder by extending `Symbology`, parsing/string conversion, `encode()`, capability lists, UI type list, tests, and protocol docs.
- Add PDF417 as a new portable encoder while retaining raw upload compatibility.
- Add named preset entry through a touch keyboard modal.
- Add test sequences/playlists that advance through symbols on scanner feedback or a timed schedule.
- Add Wi-Fi/BLE transports that feed the same command dispatcher.
- Add scanner-to-device feedback input for automated pass/fail correlation.
- Move to ESP32-S3 hardware for true USB CDC/HID/vendor-class functionality without a bridge.
