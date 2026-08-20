# Security Policy

## Supported versions

This project is pre-1.0 (firmware `0.1.0`, protocol `1.0`). Security fixes land
on `main` only; there are no maintained release branches yet.

## Reporting a vulnerability

Please report security issues privately via
[GitHub Security Advisories](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/security/advisories/new)
rather than a public issue.

Include, where relevant:

- Whether the issue is in the firmware, the USB serial protocol, the Python
  host tool, or the .NET client.
- Reproduction steps or protocol traffic (NDJSON requests/responses).
- Firmware version (from the `hello` command's `firmware` field) and hardware
  revision if applicable.

## Scope notes

- The device's USB serial protocol has no authentication — it trusts whatever
  is connected to the USB port. This is expected for a lab/bench barcode
  utility; do not expose the serial bridge to an untrusted network without
  adding your own access controls.
- Raw matrix uploads are size- and CRC-validated, but this firmware runs on
  memory-constrained hardware without the defense-in-depth of a general-purpose
  OS — treat findings that affect parsing of attacker-controlled serial input
  as high priority.
