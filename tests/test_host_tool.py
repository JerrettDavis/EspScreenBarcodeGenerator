from __future__ import annotations

import base64
import binascii
import importlib.util
import json
import pathlib
import tempfile
import unittest
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("espbarcode_host", ROOT / "tools" / "espbarcode.py")
assert SPEC and SPEC.loader
host = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = host
SPEC.loader.exec_module(host)


class HostToolTests(unittest.TestCase):
    def transfer(self) -> host.MatrixTransfer:
        packed = bytes([0b10101010, 0b10000000])  # 9 modules, continuous MSB-first
        return host.MatrixTransfer(
            width=9,
            height=1,
            linear=True,
            quiet=10,
            rotation="90",
            invert=False,
            label="unit",
            packed=packed,
            crc32=binascii.crc32(packed) & 0xFFFFFFFF,
        )

    def test_json_round_trip_and_crc(self) -> None:
        transfer = self.transfer()
        document = host.transfer_to_json(transfer)
        self.assertEqual(document["encoding"], "base64-packed-msb-first")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "matrix.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            restored = host.transfer_from_json(path)
        self.assertEqual(restored, transfer)

    def test_json_rejects_crc_tampering(self) -> None:
        transfer = self.transfer()
        document = host.transfer_to_json(transfer)
        document["data"] = base64.b64encode(b"\x00\x00").decode("ascii")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "bad.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "CRC32"):
                host.transfer_from_json(path)

    def test_p1_packing_uses_continuous_msb_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "matrix.pbm"
            path.write_text("P1\n3 2\n1 0 1\n0 1 0\n", encoding="ascii")
            transfer = host.transfer_from_pbm(path, linear=False, quiet=2, rotation="0", invert=False)
        self.assertEqual((transfer.width, transfer.height), (3, 2))
        self.assertEqual(transfer.packed, bytes([0b10101000]))

    def test_p4_non_byte_aligned_rows_are_repacked_continuously(self) -> None:
        # PBM row padding: rows are 101 and 010, represented as A0 and 40.
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "matrix.pbm"
            path.write_bytes(b"P4\n3 2\n" + bytes([0xA0, 0x40]))
            transfer = host.transfer_from_pbm(path, linear=False, quiet=1, rotation="auto", invert=False)
        self.assertEqual(transfer.packed, bytes([0b10101000]))

    def test_linear_pbm_collapses_identical_rows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "linear.pbm"
            path.write_text("P1\n4 3\n1 0 1 0\n1 0 1 0\n1 0 1 0\n", encoding="ascii")
            transfer = host.transfer_from_pbm(path, linear=True, quiet=10, rotation="90", invert=False)
        self.assertTrue(transfer.linear)
        self.assertEqual(transfer.height, 1)
        self.assertEqual(transfer.packed, bytes([0b10100000]))

    def test_linear_pbm_rejects_different_rows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "bad-linear.pbm"
            path.write_text("P1\n4 2\n1 0 1 0\n1 0 0 0\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "rows must be identical"):
                host.transfer_from_pbm(path, linear=True, quiet=10, rotation="90", invert=False)

    def test_write_pbm_preserves_module_bits(self) -> None:
        transfer = self.transfer()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "out.pbm"
            host.write_pbm(path, transfer)
            restored = host.transfer_from_pbm(path, linear=True, quiet=10, rotation="90", invert=False)
        self.assertEqual(restored.packed, transfer.packed)

    def test_transfer_validation_bounds(self) -> None:
        transfer = self.transfer()
        invalid = host.MatrixTransfer(**{**transfer.__dict__, "rotation": "45"})
        with self.assertRaisesRegex(ValueError, "rotation"):
            host.validate_transfer(invalid)
        invalid = host.MatrixTransfer(**{**transfer.__dict__, "height": 2})
        with self.assertRaisesRegex(ValueError, "one module row"):
            host.validate_transfer(invalid)


class ScriptedClient(host.EspBarcodeClient):
    def __init__(self, responses: list[dict[str, object]]) -> None:
        self.timeout = 0.1
        self._next_id = 1
        self.responses = list(responses)
        self.sent: list[dict[str, object]] = []

    def send(self, payload: dict[str, object]) -> None:
        self.sent.append(payload)

    def _read_json(self, deadline: float) -> dict[str, object]:
        del deadline
        if not self.responses:
            raise TimeoutError("script exhausted")
        return self.responses.pop(0)


class RecordingUploadClient(host.EspBarcodeClient):
    def __init__(self, *, bad_offset: bool = False) -> None:
        self.bad_offset = bad_offset
        self.calls: list[tuple[str, dict[str, object]]] = []

    def request(self, command: str, **fields: object) -> dict[str, object]:
        self.calls.append((command, fields))
        if command == "upload_begin":
            return {"ok": True, "bytes_expected": 2, "next_offset": 0}
        if command == "upload_chunk":
            chunk = base64.b64decode(str(fields["data"]), validate=True)
            expected = int(fields["offset"]) + len(chunk)
            return {
                "ok": True,
                "accepted": len(chunk),
                "next_offset": expected + (1 if self.bad_offset else 0),
            }
        if command == "upload_end":
            return {"ok": True, "crc32": int(fields["crc32"])}
        raise AssertionError(command)


class ProtocolFlowTests(unittest.TestCase):
    def transfer(self) -> host.MatrixTransfer:
        packed = bytes([0xAA, 0x80])
        return host.MatrixTransfer(
            width=9,
            height=1,
            linear=True,
            quiet=10,
            rotation="90",
            invert=False,
            label="flow",
            packed=packed,
            crc32=binascii.crc32(packed) & 0xFFFFFFFF,
        )

    def test_request_ignores_unsolicited_event_and_matches_id(self) -> None:
        client = ScriptedClient([
            {"event": "ready", "device": "EspScreenBarcodeGenerator"},
            {"id": 1, "ok": True, "cmd": "status"},
        ])
        response = client.request("status")
        self.assertEqual(response["cmd"], "status")
        self.assertEqual(client.sent, [{"id": 1, "cmd": "status"}])

    def test_request_surfaces_structured_device_error(self) -> None:
        client = ScriptedClient([
            {"id": 1, "ok": False, "error": {"code": "invalid_spec", "message": "bad data"}},
        ])
        with self.assertRaisesRegex(host.ProtocolError, "invalid_spec: bad data"):
            client.request("generate")

    def test_download_accepts_complete_non_overlapping_chunks(self) -> None:
        packed = bytes([0xAA, 0x80])
        crc = binascii.crc32(packed) & 0xFFFFFFFF
        client = ScriptedClient([
            {
                "id": 1,
                "ok": True,
                "event": "download_begin",
                "bytes": 2,
                "width": 9,
                "height": 1,
                "linear": True,
                "quiet": 10,
                "rotation": "90",
                "invert": False,
                "label": "flow",
                "crc32": crc,
            },
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 1,
             "data": base64.b64encode(packed[1:]).decode("ascii")},
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 0,
             "data": base64.b64encode(packed[:1]).decode("ascii")},
            {"id": 1, "ok": True, "event": "download_end", "bytes": 2, "crc32": crc},
        ])
        restored = client.download()
        self.assertEqual(restored.packed, packed)
        self.assertEqual(client.sent[0]["cmd"], "download")

    def test_download_rejects_missing_bytes_even_before_crc(self) -> None:
        packed = bytes([0xAA, 0x80])
        crc = binascii.crc32(packed) & 0xFFFFFFFF
        client = ScriptedClient([
            {
                "id": 1,
                "ok": True,
                "event": "download_begin",
                "bytes": 2,
                "width": 9,
                "height": 1,
                "linear": True,
                "quiet": 10,
                "rotation": "90",
                "invert": False,
                "crc32": crc,
            },
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 0,
             "data": base64.b64encode(packed[:1]).decode("ascii")},
            {"id": 1, "ok": True, "event": "download_end", "bytes": 2, "crc32": crc},
        ])
        with self.assertRaisesRegex(host.ProtocolError, "before every byte"):
            client.download()

    def test_download_rejects_malformed_base64(self) -> None:
        packed = bytes([0xAA, 0x80])
        crc = binascii.crc32(packed) & 0xFFFFFFFF
        client = ScriptedClient([
            {
                "id": 1,
                "ok": True,
                "event": "download_begin",
                "bytes": 2,
                "width": 9,
                "height": 1,
                "linear": True,
                "quiet": 10,
                "rotation": "90",
                "invert": False,
                "crc32": crc,
            },
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 0, "data": "%%%"},
        ])
        with self.assertRaisesRegex(host.ProtocolError, "malformed"):
            client.download()

    def test_download_rejects_out_of_range_chunk(self) -> None:
        packed = bytes([0xAA, 0x80])
        crc = binascii.crc32(packed) & 0xFFFFFFFF
        client = ScriptedClient([
            {
                "id": 1,
                "ok": True,
                "event": "download_begin",
                "bytes": 2,
                "width": 9,
                "height": 1,
                "linear": True,
                "quiet": 10,
                "rotation": "90",
                "invert": False,
                "crc32": crc,
            },
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 2,
             "data": base64.b64encode(b"x").decode("ascii")},
        ])
        with self.assertRaisesRegex(host.ProtocolError, "outside"):
            client.download()

    def test_download_rejects_overlapping_chunks(self) -> None:
        packed = bytes([0xAA, 0x80])
        crc = binascii.crc32(packed) & 0xFFFFFFFF
        client = ScriptedClient([
            {
                "id": 1,
                "ok": True,
                "event": "download_begin",
                "bytes": 2,
                "width": 9,
                "height": 1,
                "linear": True,
                "quiet": 10,
                "rotation": "90",
                "invert": False,
                "crc32": crc,
            },
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 0,
             "data": base64.b64encode(packed).decode("ascii")},
            {"id": 1, "ok": True, "event": "download_chunk", "offset": 1,
             "data": base64.b64encode(packed[1:]).decode("ascii")},
        ])
        with self.assertRaisesRegex(host.ProtocolError, "overlap"):
            client.download()

    def test_upload_validates_device_offsets_and_crc(self) -> None:
        client = RecordingUploadClient()
        response = client.upload(self.transfer(), chunk_bytes=48)
        self.assertTrue(response["ok"])
        self.assertEqual([command for command, _ in client.calls],
                         ["upload_begin", "upload_chunk", "upload_end"])

    def test_upload_rejects_unexpected_acknowledged_offset(self) -> None:
        client = RecordingUploadClient(bad_offset=True)
        with self.assertRaisesRegex(host.ProtocolError, "unexpected upload offset"):
            client.upload(self.transfer(), chunk_bytes=48)


if __name__ == "__main__":
    unittest.main()
