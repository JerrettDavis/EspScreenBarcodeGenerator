#!/usr/bin/env python3
"""Generate symbols with the portable firmware core and decode them independently with MuPDF."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import tempfile
from dataclasses import dataclass

try:
    import pymupdf
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyMuPDF is required for independent symbol validation") from exc


@dataclass(frozen=True)
class Case:
    name: str
    symbology: str
    data: str
    expected: str
    decoder_type: str
    extra: tuple[str, ...] = ()


CASES = (
    Case("Code 128", "code128", "Hello-123", "Hello-123", "code128"),
    Case(
        "GS1-128 FNC1",
        "gs1-128",
        "0109501101530003{FNC1}10ABC",
        "0109501101530003<GS>10ABC",
        "code128",
    ),
    Case("Code 39", "code39", "LAB-39", "LAB-39", "code39"),
    Case("EAN-13", "ean13", "590123412345", "5901234123457", "ean13"),
    Case("EAN-8", "ean8", "5512345", "55123457", "ean8"),
    Case("UPC-A", "upca", "03600029145", "036000291452", "upca"),
    Case("ITF", "itf", "12345670", "12345670", "itf"),
    Case("ITF-14", "itf14", "1001234500001", "10012345000017", "itf"),
    Case("Codabar", "codabar", "A12345B", "A12345B", "codabar"),
    Case("Data Matrix ECC200", "datamatrix", "DM-ROUNDTRIP-123", "DM-ROUNDTRIP-123", "datamatrix"),
    Case("Rectangular Data Matrix", "datamatrix", "RECT-123", "RECT-123", "datamatrix", ("--rect",)),
    Case("Aztec", "aztec", "AZTEC-ROUNDTRIP-123", "AZTEC-ROUNDTRIP-123", "aztec"),
    Case("Aztec Rune", "aztec", "42", "042", "aztec", ("--aztec-layers", "0")),
)


def decode(path: pathlib.Path) -> tuple[str, str]:
    pixmap = pymupdf.Pixmap(str(path))
    value, barcode_type = pymupdf.mupdf.fz_decode_barcode_from_pixmap2(pixmap.this, 0)
    name = pymupdf.mupdf.fz_string_from_barcode_type(barcode_type)
    return value, name


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()

    rows: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="espbarcode-symbols-") as temporary:
        directory = pathlib.Path(temporary)
        for index, case in enumerate(CASES):
            output = directory / f"{index:02d}-{case.symbology}.pbm"
            command = [
                str(args.cli),
                "--type",
                case.symbology,
                "--data",
                case.data,
                "--output",
                str(output),
                "--scale",
                "8",
                *case.extra,
            ]
            generated = subprocess.run(command, capture_output=True, text=True, check=False)
            if generated.returncode != 0:
                raise AssertionError(f"{case.name} generation failed: {generated.stderr.strip()}")
            metadata = json.loads(generated.stdout)
            decoded, decoder_type = decode(output)
            if decoded != case.expected:
                raise AssertionError(f"{case.name}: expected {case.expected!r}, decoded {decoded!r}")
            if decoder_type != case.decoder_type:
                raise AssertionError(f"{case.name}: expected decoder type {case.decoder_type}, got {decoder_type}")
            rows.append(
                {
                    "case": case.name,
                    "encoder_type": case.symbology,
                    "decoder_type": decoder_type,
                    "decoded": decoded,
                    "width_modules": metadata["width_modules"],
                    "height_modules": metadata["height_modules"],
                    "result": "pass",
                }
            )
            print(f"PASS {case.name}: {decoded!r} ({decoder_type})")

    report = {
        "schema": "esp-screen-barcode-validation/1",
        "independent_decoder": f"MuPDF via PyMuPDF {pymupdf.VersionBind}",
        "cases": rows,
        "passed": len(rows),
        "failed": 0,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Independent decoder validation passed: {len(rows)} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
