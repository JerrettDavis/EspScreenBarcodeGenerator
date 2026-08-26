#!/usr/bin/env python3
"""Host utility for EspScreenBarcodeGenerator protocol 1.0."""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import pathlib
import sys
import time
from dataclasses import dataclass
from typing import Any, BinaryIO, Iterable

try:
    import serial  # type: ignore
    import serial.tools.list_ports  # type: ignore
except ImportError:  # pragma: no cover - handled at runtime
    serial = None


class ProtocolError(RuntimeError):
    pass


@dataclass
class MatrixTransfer:
    width: int
    height: int
    linear: bool
    quiet: int
    rotation: str
    invert: bool
    label: str
    packed: bytes
    crc32: int


class EspBarcodeClient:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 3.0) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required: python -m pip install -r tools/requirements.txt")
        self.serial = serial.Serial(port=port, baudrate=baud, timeout=0.15, write_timeout=timeout)
        self.timeout = timeout
        self._next_id = 1
        # Opening the CH340 toggles DTR/RTS and resets the ESP32. The full firmware's
        # measured cold boot (TFT, LittleFS, BLE, ESP-NOW) exceeds 1.5 seconds on both
        # supported lab boards; sending during that window is silently lost. Match the
        # .NET transport's conservative boot margin before discarding boot chatter.
        time.sleep(2.5)
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.serial.close()

    def __enter__(self) -> "EspBarcodeClient":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def send(self, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"
        self.serial.write(raw)
        self.serial.flush()

    def _read_json(self, deadline: float) -> dict[str, Any]:
        while time.monotonic() < deadline:
            line = self.serial.readline()
            if not line:
                continue
            try:
                decoded = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                # Non-protocol chatter (e.g. boot-time log lines emitted after a
                # DTR-triggered reset) shares the UART; skip it and keep listening.
                continue
            if isinstance(decoded, dict):
                return decoded
        raise TimeoutError("timed out waiting for device response")

    def request(self, command: str, **fields: Any) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        self.send({"id": request_id, "cmd": command, **fields})
        deadline = time.monotonic() + self.timeout
        while True:
            response = self._read_json(deadline)
            if response.get("id") != request_id:
                continue
            if response.get("ok") is False:
                error = response.get("error") or {}
                raise ProtocolError(f"{error.get('code', 'device_error')}: {error.get('message', response)}")
            return response

    def download(self, chunk_bytes: int = 384) -> MatrixTransfer:
        if not 48 <= chunk_bytes <= 768:
            raise ValueError("chunk_bytes must be between 48 and 768")
        request_id = self._next_id
        self._next_id += 1
        self.send({"id": request_id, "cmd": "download", "chunk_bytes": chunk_bytes})
        deadline = time.monotonic() + max(self.timeout, 10.0)
        metadata: dict[str, Any] | None = None
        packed = bytearray()
        received = bytearray()
        while True:
            response = self._read_json(deadline)
            if response.get("id") != request_id:
                continue
            if response.get("ok") is False:
                error = response.get("error") or {}
                raise ProtocolError(f"{error.get('code', 'device_error')}: {error.get('message', response)}")
            event = response.get("event")
            if event == "download_begin":
                if metadata is not None:
                    raise ProtocolError("duplicate download metadata")
                total = int(response["bytes"])
                if total < 1 or total > 32768:
                    raise ProtocolError("download declared an invalid byte count")
                metadata = response
                packed = bytearray(total)
                received = bytearray(total)
            elif event == "download_chunk":
                if metadata is None:
                    raise ProtocolError("download chunk arrived before metadata")
                try:
                    offset = int(response["offset"])
                    chunk = base64.b64decode(response["data"], validate=True)
                except (KeyError, TypeError, ValueError, binascii.Error) as exc:
                    raise ProtocolError("download chunk is malformed") from exc
                end = offset + len(chunk)
                if not chunk or offset < 0 or end > len(packed):
                    raise ProtocolError("download chunk is empty or outside the declared payload")
                if any(received[offset:end]):
                    raise ProtocolError("download chunks overlap or were duplicated")
                packed[offset:end] = chunk
                received[offset:end] = b"\x01" * len(chunk)
            elif event == "download_end":
                if metadata is None:
                    raise ProtocolError("download ended without metadata")
                if not all(received):
                    raise ProtocolError("download ended before every byte was received")
                expected = int(response["crc32"])
                metadata_crc = int(metadata.get("crc32", expected))
                if metadata_crc != expected:
                    raise ProtocolError("download metadata and completion CRCs disagree")
                if int(response.get("bytes", len(packed))) != len(packed):
                    raise ProtocolError("download completion byte count disagrees with metadata")
                actual = binascii.crc32(packed) & 0xFFFFFFFF
                if actual != expected:
                    raise ProtocolError(f"download CRC mismatch: expected {expected:#010x}, got {actual:#010x}")
                return validate_transfer(MatrixTransfer(
                    width=int(metadata["width"]),
                    height=int(metadata["height"]),
                    linear=bool(metadata["linear"]),
                    quiet=int(metadata["quiet"]),
                    rotation=str(metadata["rotation"]),
                    invert=bool(metadata["invert"]),
                    label=str(metadata.get("label", "")),
                    packed=bytes(packed),
                    crc32=actual,
                ))

    def upload(self, matrix: MatrixTransfer, display: bool = True, chunk_bytes: int = 384) -> dict[str, Any]:
        matrix = validate_transfer(matrix)
        if not 48 <= chunk_bytes <= 768:
            raise ValueError("chunk_bytes must be between 48 and 768")
        begin = self.request(
            "upload_begin",
            width=matrix.width,
            height=matrix.height,
            linear=matrix.linear,
            quiet=matrix.quiet,
            rotation=matrix.rotation,
            invert=matrix.invert,
            label=matrix.label,
            display=display,
        )
        if int(begin.get("bytes_expected", len(matrix.packed))) != len(matrix.packed):
            raise ProtocolError("device acknowledged an unexpected upload size")
        if int(begin.get("next_offset", 0)) != 0:
            raise ProtocolError("device requested an invalid initial upload offset")
        for offset in range(0, len(matrix.packed), chunk_bytes):
            chunk = matrix.packed[offset : offset + chunk_bytes]
            response = self.request(
                "upload_chunk",
                offset=offset,
                data=base64.b64encode(chunk).decode("ascii"),
            )
            expected_next = offset + len(chunk)
            if int(response.get("accepted", len(chunk))) != len(chunk):
                raise ProtocolError("device acknowledged an unexpected upload chunk size")
            if int(response.get("next_offset", expected_next)) != expected_next:
                raise ProtocolError("device acknowledged an unexpected upload offset")
        checksum = binascii.crc32(matrix.packed) & 0xFFFFFFFF
        response = self.request("upload_end", crc32=checksum)
        if int(response.get("crc32", checksum)) != checksum:
            raise ProtocolError("device reported a different upload CRC32")
        return response


def find_default_port() -> str:
    if serial is None:
        raise RuntimeError("pyserial is not installed")
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        raise RuntimeError("no serial ports found; pass --port explicitly")
    preferred = [
        port for port in ports
        if any(token in (port.description or "").lower() for token in ("cp210", "ch340", "usb serial", "uart"))
    ]
    return (preferred or ports)[0].device


def validate_transfer(matrix: MatrixTransfer) -> MatrixTransfer:
    if not (1 <= matrix.width <= 512 and 1 <= matrix.height <= 512):
        raise ValueError("matrix dimensions must be between 1 and 512 modules")
    if matrix.linear and matrix.height != 1:
        raise ValueError("linear matrices must have exactly one module row")
    required = (matrix.width * matrix.height + 7) // 8
    if required > 32768:
        raise ValueError("packed matrix exceeds the device's 32768-byte limit")
    if len(matrix.packed) != required:
        raise ValueError(f"matrix requires {required} packed bytes but contains {len(matrix.packed)}")
    if matrix.quiet < 0 or matrix.quiet > 32:
        raise ValueError("quiet zone must be between 0 and 32 modules")
    if matrix.rotation not in {"auto", "0", "90", "180", "270"}:
        raise ValueError("rotation must be auto, 0, 90, 180, or 270")
    actual = binascii.crc32(matrix.packed) & 0xFFFFFFFF
    if matrix.crc32 != actual:
        raise ValueError("matrix CRC32 does not match its packed payload")
    return matrix


def transfer_to_json(matrix: MatrixTransfer) -> dict[str, Any]:
    matrix = validate_transfer(matrix)
    return {
        "schema": "esp-screen-barcode-matrix/1",
        "width": matrix.width,
        "height": matrix.height,
        "linear": matrix.linear,
        "quiet": matrix.quiet,
        "rotation": matrix.rotation,
        "invert": matrix.invert,
        "label": matrix.label,
        "encoding": "base64-packed-msb-first",
        "crc32": matrix.crc32,
        "data": base64.b64encode(matrix.packed).decode("ascii"),
    }


def transfer_from_json(path: pathlib.Path) -> MatrixTransfer:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != "esp-screen-barcode-matrix/1":
        raise ValueError("unsupported matrix JSON schema")
    packed = base64.b64decode(document["data"], validate=True)
    expected = int(document.get("crc32", binascii.crc32(packed) & 0xFFFFFFFF))
    actual = binascii.crc32(packed) & 0xFFFFFFFF
    if actual != expected:
        raise ValueError("matrix file CRC32 does not match its payload")
    required = (int(document["width"]) * int(document["height"]) + 7) // 8
    if len(packed) != required:
        raise ValueError(f"matrix requires {required} packed bytes but file contains {len(packed)}")
    return validate_transfer(MatrixTransfer(
        width=int(document["width"]),
        height=int(document["height"]),
        linear=bool(document.get("linear", False)),
        quiet=int(document.get("quiet", 4)),
        rotation=str(document.get("rotation", "auto")),
        invert=bool(document.get("invert", False)),
        label=str(document.get("label", path.stem)),
        packed=packed,
        crc32=actual,
    ))


def transfer_from_pbm(path: pathlib.Path, *, linear: bool, quiet: int, rotation: str, invert: bool) -> MatrixTransfer:
    raw = path.read_bytes()
    if not raw.startswith((b"P1", b"P4")):
        raise ValueError("only PBM P1 or P4 module matrices are supported without Pillow")

    def tokens(data: bytes) -> Iterable[bytes]:
        for line in data.splitlines():
            line = line.split(b"#", 1)[0]
            yield from line.split()

    iterator = iter(tokens(raw))
    magic = next(iterator)
    width = int(next(iterator))
    height = int(next(iterator))
    if magic == b"P1":
        bits = [int(next(iterator)) for _ in range(width * height)]
    else:
        # Locate binary raster after parsing exactly three header tokens.
        position = 0
        header_tokens = 0
        in_comment = False
        while position < len(raw) and header_tokens < 3:
            byte = raw[position]
            if in_comment:
                if byte in (10, 13):
                    in_comment = False
                position += 1
                continue
            if byte == 35:
                in_comment = True
            elif byte not in b" \t\r\n":
                header_tokens += 1
                while position < len(raw) and raw[position] not in b" \t\r\n":
                    position += 1
                continue
            position += 1
        while position < len(raw) and raw[position] in b" \t\r\n":
            position += 1
        row_bytes = (width + 7) // 8
        raster = raw[position : position + row_bytes * height]
        if len(raster) != row_bytes * height:
            raise ValueError("truncated PBM raster")
        bits = [
            (raster[y * row_bytes + x // 8] >> (7 - x % 8)) & 1
            for y in range(height)
            for x in range(width)
        ]

    if width < 1 or height < 1:
        raise ValueError("PBM dimensions must be positive")
    if width > 512 or height > 512:
        raise ValueError("PBM dimensions exceed the device's 512-module limit")
    if len(bits) != width * height or any(bit not in (0, 1) for bit in bits):
        raise ValueError("PBM raster contains invalid or incomplete module data")

    matrix_height = height
    if linear:
        first_row = bits[:width]
        for row in range(1, height):
            if bits[row * width : (row + 1) * width] != first_row:
                raise ValueError("linear PBM rows must be identical so they can collapse to one module row")
        bits = first_row
        matrix_height = 1

    packed = bytearray((width * matrix_height + 7) // 8)
    for index, bit in enumerate(bits):
        if bit:
            packed[index // 8] |= 1 << (7 - index % 8)
    crc = binascii.crc32(packed) & 0xFFFFFFFF
    return validate_transfer(MatrixTransfer(
        width, matrix_height, linear, quiet, rotation, invert, path.stem, bytes(packed), crc
    ))


def write_pbm(path: pathlib.Path, matrix: MatrixTransfer) -> None:
    row_bytes = (matrix.width + 7) // 8
    # Packed protocol bits are continuous across rows; PBM P4 pads each row.
    raster = bytearray(row_bytes * matrix.height)
    for y in range(matrix.height):
        for x in range(matrix.width):
            source = y * matrix.width + x
            if matrix.packed[source // 8] & (1 << (7 - source % 8)):
                raster[y * row_bytes + x // 8] |= 1 << (7 - x % 8)
    path.write_bytes(f"P4\n{matrix.width} {matrix.height}\n".encode("ascii") + raster)


def print_json(value: Any) -> None:
    print(json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port; auto-detected when omitted")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    commands = parser.add_subparsers(dest="command", required=True)

    for name in ("hello", "capabilities", "status", "close", "home", "list", "reboot"):
        commands.add_parser(name)

    generate = commands.add_parser("generate")
    generate.add_argument("type")
    generate.add_argument("data")
    generate.add_argument("--ecc", default="M")
    generate.add_argument("--rotation", default="auto")
    generate.add_argument("--quiet", type=int, default=-1)
    generate.add_argument("--min-module", type=int, default=2)
    generate.add_argument("--rect", action="store_true")
    generate.add_argument("--invert", action="store_true")
    generate.add_argument("--no-checksum", action="store_true")
    generate.add_argument("--no-display", action="store_true")
    generate.add_argument("--save-as")
    generate.add_argument("--aztec-security", type=int, default=23)
    generate.add_argument("--aztec-layers", type=int, default=1)
    generate.add_argument("--qr-min-version", type=int, default=1)
    generate.add_argument("--qr-max-version", type=int, default=20)

    display = commands.add_parser("display")
    display.add_argument("--name")
    for name in ("save", "delete"):
        sub = commands.add_parser(name)
        sub.add_argument("name")
    load = commands.add_parser("load")
    load.add_argument("name")
    load.add_argument("--display", action="store_true")
    backlight = commands.add_parser("backlight")
    backlight.add_argument("state", choices=("on", "off"))

    download = commands.add_parser("download")
    download.add_argument("output", type=pathlib.Path)
    download.add_argument("--pbm", type=pathlib.Path)

    upload = commands.add_parser("upload")
    upload.add_argument("input", type=pathlib.Path)
    upload.add_argument("--linear", action="store_true", help="PBM only: treat rows as one-dimensional bars")
    upload.add_argument("--quiet", type=int, default=4)
    upload.add_argument("--rotation", default="auto")
    upload.add_argument("--invert", action="store_true")
    upload.add_argument("--no-display", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    port = args.port or find_default_port()
    with EspBarcodeClient(port, args.baud, args.timeout) as client:
        command = args.command
        if command in {"hello", "capabilities", "status", "close", "home", "list", "reboot"}:
            print_json(client.request(command))
        elif command == "generate":
            data = args.data
            if data.startswith("@"):
                data = pathlib.Path(data[1:]).read_text(encoding="utf-8")
            print_json(client.request(
                "generate",
                type=args.type,
                data=data,
                ecc=args.ecc,
                rotation=args.rotation,
                quiet=args.quiet,
                min_module=args.min_module,
                rect=args.rect,
                invert=args.invert,
                checksum=not args.no_checksum,
                display=not args.no_display,
                save_as=args.save_as,
                aztec_security=args.aztec_security,
                aztec_layers=args.aztec_layers,
                qr_min_version=args.qr_min_version,
                qr_max_version=args.qr_max_version,
            ))
        elif command == "display":
            print_json(client.request("display", **({"name": args.name} if args.name else {})))
        elif command in {"save", "delete"}:
            print_json(client.request(command, name=args.name))
        elif command == "load":
            print_json(client.request("load", name=args.name, display=args.display))
        elif command == "backlight":
            print_json(client.request("backlight", on=args.state == "on"))
        elif command == "download":
            matrix = client.download()
            args.output.write_text(json.dumps(transfer_to_json(matrix), indent=2), encoding="utf-8")
            if args.pbm:
                write_pbm(args.pbm, matrix)
            print_json({"ok": True, "output": str(args.output), "bytes": len(matrix.packed), "crc32": matrix.crc32})
        elif command == "upload":
            if args.input.suffix.lower() == ".json":
                matrix = transfer_from_json(args.input)
            else:
                matrix = transfer_from_pbm(
                    args.input,
                    linear=args.linear,
                    quiet=args.quiet,
                    rotation=args.rotation,
                    invert=args.invert,
                )
            print_json(client.upload(matrix, display=not args.no_display))
        else:  # pragma: no cover
            raise AssertionError(command)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProtocolError, TimeoutError, OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
