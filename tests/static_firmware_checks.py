#!/usr/bin/env python3
"""Guard the known-good display/touch contract and required protocol surface."""

from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(text: str, pattern: str, source: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        raise AssertionError(f"{source}: required pattern not found: {pattern}")


def main() -> int:
    ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    config = (ROOT / "include" / "app_config.h").read_text(encoding="utf-8")
    protocol = ((ROOT / "src" / "JsonCommandCodec.cpp").read_text(encoding="utf-8") +
                (ROOT / "src" / "SerialLegacyEndpoint.cpp").read_text(encoding="utf-8") +
                (ROOT / "lib" / "EspLinkCore" / "src" / "ControlProtocolEngine.cpp").read_text(encoding="utf-8"))
    application = (ROOT / "src" / "BarcodeApplication.cpp").read_text(encoding="utf-8")

    checks = {
        r"platform\s*=\s*platformio/espressif32@7\.0\.1": "pinned ESP32 platform",
        r"-D ST7796_DRIVER=1": "ST7796 driver",
        r"-D TFT_WIDTH=320": "display width",
        r"-D TFT_HEIGHT=480": "display height",
        r"-D TFT_MISO=12": "HSPI MISO",
        r"-D TFT_MOSI=13": "HSPI MOSI",
        r"-D TFT_SCLK=14": "HSPI SCLK",
        r"-D TFT_CS=15": "TFT chip select",
        r"-D TFT_DC=2": "TFT data/command",
        r"-D TFT_BL=27": "backlight",
        r"-D TOUCH_CS=33": "touch chip select",
        r"-D TFT_RGB_ORDER=TFT_BGR": "BGR ordering",
        r"-D USE_HSPI_PORT": "HSPI selection",
        r"SPI_TOUCH_FREQUENCY=2500000": "touch SPI frequency",
    }
    for pattern, label in checks.items():
        require(ini, pattern, f"platformio.ini ({label})")

    if "lvgl" in ini.lower():
        raise AssertionError("platformio.ini: unused LVGL dependency must not be reintroduced")
    require(config, r"kTouchCalibration\[5\]\s*=\s*\{275,\s*3620,\s*264,\s*3532,\s*4\}", "app_config.h")
    require(application, r"applyOrientationForView", "BarcodeApplication.cpp")
    require(application, r"setTouch\(", "BarcodeApplication.cpp")
    require(application, r"modulePixels", "BarcodeApplication.cpp")

    commands = {
        "hello", "capabilities", "status", "generate", "display", "close", "home",
        "save", "load", "delete", "list", "upload_begin", "upload_chunk", "upload_end",
        "upload_abort", "download", "backlight", "orientation", "reboot",
    }
    missing = sorted(command for command in commands if f'command == "{command}"' not in protocol and f'"{command}"' not in protocol)
    if missing:
        raise AssertionError(f"firmware protocol sources: missing required commands: {', '.join(missing)}")

    for token in ("base64-packed-msb-first", "crc32", "line_too_long", "unexpected_offset"):
        if token not in protocol:
            raise AssertionError(f"firmware protocol sources: missing protocol safety token {token}")

    print("Static firmware contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
