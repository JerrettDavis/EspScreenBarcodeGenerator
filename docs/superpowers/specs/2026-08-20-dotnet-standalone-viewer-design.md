# Standalone .NET barcode generator & viewer — design

Date: 2026-08-20
Status: Approved

## Problem

The ESP32 device (`EspScreenBarcodeGenerator` firmware) generates barcode
module patterns on-device and displays them full-screen so a handheld
scanner can be tested against them. The existing `dotnet/` client library
and CLI only talk to a *connected* device over USB serial.

This feature adds a standalone .NET tool that replicates the ESP's
generation and display behavior **without any ESP hardware**: a CLI that
generates any supported barcode type from parameters or a file and writes
it to disk and/or opens it, and a minimal WPF viewer window that a CLI
invocation (or a test orchestrator) can push new barcodes into on the fly,
for the same "scan straight off the screen" workflow the physical device
supports.

## Non-goals

- No barcode *creation* UI in the GUI (view-only; the ESP's touch keyboard
  is out of scope for this revision — noted as a possible future
  addition).
- No dependency on `EspBarcode.Client`'s serial transport; this tool never
  talks to the ESP.
- No cross-platform GUI. WPF is Windows-only, matching this project's
  existing Windows-first stance (serial port workflow, PowerShell quick
  start).

## Architecture

Three new projects, added as siblings to the existing `EspBarcode.Client` /
`EspBarcode.Cli` in `EspScreenBarcodeGenerator.slnx`:

```
dotnet/
  src/
    EspBarcode.Client/          (existing) ESP USB-serial protocol client
    EspBarcode.Cli/              (existing) demo CLI against a real device
    EspBarcode.Generator/        (new) barcode generation + layout core, no UI/IPC
    EspBarcode.Viewer.Cli/       (new) standalone generation CLI
    EspBarcode.Viewer.Gui/       (new, net10.0-windows, WPF) display-only viewer window
  tests/
    EspBarcode.Client.Tests/     (existing)
    EspBarcode.Generator.Tests/  (new)
    EspBarcode.Viewer.Cli.Tests/ (new)
```

`EspBarcode.Generator` is the "single core package" the goal calls for:
all encoding, check-digit, layout, and image-rendering logic lives there
and is referenced by both `EspBarcode.Viewer.Cli` and
`EspBarcode.Viewer.Gui`. Neither the CLI nor the GUI re-implement any
generation logic.

### CI impact

`EspBarcode.Viewer.Gui` targets `net10.0-windows` (`UseWPF=true`), which
cannot build on Linux. `.github/workflows/dotnet-ci.yml` currently runs on
`ubuntu-latest`; it will be changed to `windows-latest` so the whole
solution (including the existing serial client, which is also
Windows-primary) continues to build and test green on every push/PR to
`main`.

## Components

### `EspBarcode.Generator`

- **`BarcodeType`** — its own enum, not shared with `EspBarcode.Client`'s:
  the 13 ESP on-device symbologies (Qr, DataMatrix, Aztec, Code128,
  Gs1_128, Code39, UpcA, Ean13, Ean8, Itf, Itf14, Codabar, Msi) plus
  `Pdf417` (host-only, matching the existing `license` CLI scenario's
  ZXing.Net PDF417 pattern). Wire-value strings match
  `docs/PROTOCOL.md`/`EspBarcode.Client.BarcodeTypeExtensions` exactly
  (`qr`, `datamatrix`, ... `pdf417`), so the option vocabulary is
  identical across firmware, Python tool, ESP .NET client, and this new
  tool.
- **`BarcodeSpec`** — record mirroring the ESP `generate` command's fields
  one-to-one: `Type`, `Data`, `Ecc` (`L`/`M`/`Q`/`H`, QR only), `Rotation`
  (`auto`/`0`/`90`/`180`/`270`), `Quiet` (`-1` = symbology default, else
  0-32), `MinModule` (1-8, default 2), `Rectangular` (Data Matrix),
  `Invert`, `Checksum` (MSI Mod 10 opt-out), `QrMinVersion`/
  `QrMaxVersion` (1-40), `AztecSecurity` (1-90), `AztecLayers` (0-32; `0`
  requests an Aztec Rune for numeric 0-255 input). Same defaults as the
  firmware.
- **`RawMatrix`** — moved here from `EspBarcode.Client` (identical type,
  same packing contract used by the raw-matrix upload protocol). Sole
  module-matrix representation in the repo — `EspBarcode.Client` now
  references `EspBarcode.Generator` and reuses this type for its
  raw-matrix upload path instead of carrying its own copy.
- **`BarcodeGenerator.Encode(BarcodeSpec) -> RawMatrix`** — dispatches to
  a per-symbology encoder:
  - QR, Data Matrix, Aztec (non-Rune), Code 128, GS1-128, Code 39, Codabar,
    ITF, PDF417: thin wrappers around ZXing.Net's `MultiFormatWriter`
    with hints (`ERROR_CORRECTION`, `MARGIN`, `DATA_MATRIX_SHAPE`
    `FORCE_RECTANGLE`/`FORCE_NONE`, `AZTEC_LAYERS`, `GS1_FORMAT`,
    `QR_VERSION`). Confirmed against ZXing.Net 0.16.11 source
    (`Source/lib/MultiFormatWriter.cs` and friends).
  - UPC-A, EAN-13, EAN-8, ITF-14, MSI: check-digit calculation ported
    from the same rules the firmware documents (accept short input,
    compute/append check digit; validate full-length input) before
    handing off to the matching ZXing.Net writer (`MSIWriter` does
    support MSI *encoding* — confirmed in source — but does not compute
    the check digit itself, so `EspBarcode.Generator` does).
  - **Aztec Rune** (`AztecLayers == 0`): no ZXing.Net writer exists for
    this upstream (confirmed: the Java zxing project only ever tracked
    Rune *decoding*, never encoding). Implemented as a small hand-rolled
    encoder in `EspBarcode.Generator` per the published Aztec Rune layout
    (11x11 compact Aztec-style symbol encoding a single mode message for
    a 0-255 value) — this is the one symbology needing custom code,
    mirroring the firmware's own single host-only gap (PDF417).
- **`ScreenFitLayout.Fit(RawMatrix, targetWidth, targetHeight, rotation)
  -> RenderedLayout`** — reimplementation of the firmware's
  `calculateLayout` (`docs/ARCHITECTURE.md`, "Rendering pipeline"): picks
  the largest integer module scale that fits the target canvas at or
  above `MinModule`, and for `rotation: auto` evaluates both the
  un-rotated and 90-degree orientations and picks the better fit — same
  algorithm, parameterized by target canvas size instead of the ESP's
  fixed 320x480. `RenderedLayout` carries the (possibly rotated) matrix,
  integer scale, and centering offsets; nothing is resized/antialiased.
- **`BarcodeImageRenderer.Render(RenderedLayout, invert) -> byte[]`
  (PNG)** — `System.Drawing.Common`-based, draws each module as an exact
  `scale x scale` rectangle, matching the firmware's "no resized bitmap"
  principle.
- **`PayloadSource`** — resolves a CLI payload argument: a literal string,
  or `@path` to read UTF-8 text from a file, matching the convention
  already used by `tools/espbarcode.py`.

### `EspBarcode.Viewer.Cli`

Hand-parsed args, matching `EspBarcode.Cli`'s existing style (no new
arg-parsing dependency):

```
espbarcode-viewer generate <type> <data|@file> [options...] [--out path.png] [--open none|system|viewer]
espbarcode-viewer close [--viewer-port N]
espbarcode-viewer list-types
```

- `<type>` / `[options...]` — same names/semantics as the ESP protocol
  (`--ecc`, `--rotation`, `--quiet`, `--min-module`, `--rect`, `--invert`,
  `--no-checksum`, `--qr-min-version`, `--qr-max-version`,
  `--aztec-security`, `--aztec-layers`).
- `--out path.png` — writes the rendered PNG. Optional; if `--open
  system` is requested without `--out`, a temp file is used.
- `--open none` (default) / `--open system` / `--open viewer`:
  - `none`: only `--out` (if given) happens.
  - `system`: renders to `--out` or a temp PNG, then
    `Process.Start(path) { UseShellExecute = true }`.
  - `viewer`: no file is written unless `--out` was also explicitly
    given; the `BarcodeSpec` is POSTed as JSON to the running
    `EspBarcode.Viewer.Gui` instance instead (see IPC below).
- `close` — POSTs `/close` to the viewer, which exits the GUI process.
- Exit codes / error reporting follow `EspBarcode.Cli`'s existing pattern
  (stderr message, non-zero exit).

### `EspBarcode.Viewer.Gui` (WPF)

- Single `MainWindow`: a normal resizable window (title bar, standard
  chrome) containing one `Image` control that shows the current rendered
  barcode, scaled with `Stretch=Uniform` inside a checkerboard-free
  background matching `invert`.
- Hosts a minimal ASP.NET Core `WebApplication` (Kestrel) bound to
  `127.0.0.1:<port>` (default port `47823`, overridable via `--port` /
  `ESP_BARCODE_VIEWER_PORT`) on a background thread, started alongside
  the WPF `Application`:
  - `GET /health` → `200 OK` (used by the CLI to detect an already-running
    instance before deciding whether to launch a new process).
  - `POST /render` → body is `BarcodeSpec` JSON. Stores it as "current
    spec", regenerates the `RawMatrix` via `EspBarcode.Generator`, lays it
    out against the window's current client size, updates the displayed
    image (marshaled onto the WPF dispatcher), and brings the window to
    the foreground. Returns `200 OK` with basic result info (matrix
    dimensions) or `400` with an error message on invalid spec/generation
    failure.
  - `POST /close` → exits the application.
- On `SizeChanged`, if a "current spec" exists, re-runs
  `ScreenFitLayout.Fit` against the new client size and redraws — this is
  the "rerender on the fly" behavior: both new specs from the CLI and
  window resizes go through the same live re-layout path.
- If the configured port is already bound (a second instance was
  launched, e.g. by double-clicking the exe), show a message box
  ("A viewer is already running.") and exit rather than failing silently
  or trying to negotiate a new port.

### CLI ↔ GUI process/IPC contract

- The CLI never draws anything for viewer mode — it always sends the
  `BarcodeSpec`, not an image, which is why generation has to be
  reachable from the GUI process too (shared core, not shared image
  bytes).
- Before launching a new GUI process, the CLI probes `GET
  http://127.0.0.1:<port>/health` with a short timeout. If healthy, it
  reuses that instance. Otherwise it starts
  `EspBarcode.Viewer.Gui(.exe)`, located next to its own executable by
  default (overridable via `--viewer-exe` or
  `ESP_BARCODE_VIEWER_EXE_PATH`, for running from source/dev builds), and
  polls `/health` (bounded retries/timeout) before POSTing `/render`.

## Error handling

Mirrors the ESP's own failure modes (`docs/ARCHITECTURE.md`, "Failure
modes"):

- Invalid payload or check digit → generation fails with a clear message,
  non-zero CLI exit / `400` from `/render`.
- Symbol too dense for the target canvas at the requested `min_module` →
  explicit failure, never a silently shrunk/antialiased image.
- Unknown symbology or malformed option → clear validation error before
  any encoder runs.
- GUI unreachable (`--open viewer` but the process fails to start/bind) →
  CLI reports a clear error and exits non-zero; it does not silently fall
  back to system-viewer or file-only.

## Testing

- `EspBarcode.Generator.Tests`: per-symbology encode tests (including
  check-digit computation for UPC-A/EAN-13/EAN-8/ITF-14/MSI, GS1-128 FNC1
  handling, Data Matrix rectangular shape, Aztec Rune encode/decode round
  trip against a hand-verified table), `ScreenFitLayout` rotation/scale
  math (including the "too dense, fails" case), PNG renderer output
  shape, and `PayloadSource` `@file` resolution. Fully headless, runs on
  any OS.
- `EspBarcode.Viewer.Cli.Tests`: arg parsing, `--open` mode branching,
  and the health-probe/launch/POST HTTP flow against a fake
  `HttpMessageHandler` (no real GUI process spawned in tests).
- `EspBarcode.Viewer.Gui`: build-only in CI (WPF headless UI testing is
  unreliable); verified with a manual smoke run (launch, `generate
  --open viewer` twice with different specs, resize, `close`) during
  implementation per `superpowers:verification-before-completion`.
- Existing `EspBarcode.Client.Tests` continue to pass unchanged except for
  the `RawMatrix` relocation (namespace/using update only, no behavior
  change) — verified by the existing `RawMatrixTests`.

## Rollout

Implemented directly on `main` following this repo's existing workflow
(prior .NET client work was committed and pushed straight to `main`, no
PR). CI (`dotnet-ci.yml`, moved to `windows-latest`) must be green after
the final push.
