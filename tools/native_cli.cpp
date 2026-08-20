#include "EspBarcodeCore.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace espbarcode;

namespace {

bool parseBool(const std::string& value) {
    std::string lower;
    for (unsigned char c : value) lower.push_back(static_cast<char>(std::tolower(c)));
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::map<std::string, std::string> parseArgs(int argc, char** argv) {
    std::map<std::string, std::string> values;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0) throw std::runtime_error("unexpected argument: " + key);
        key.erase(0, 2);
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
            values[key] = argv[++i];
        } else {
            values[key] = "true";
        }
    }
    return values;
}

std::string get(const std::map<std::string, std::string>& args,
                const std::string& key,
                const std::string& fallback = {}) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : it->second;
}

int getInt(const std::map<std::string, std::string>& args,
           const std::string& key,
           int fallback) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : std::stoi(it->second);
}

Rotation parseRotation(const std::string& value) {
    if (value == "0") return Rotation::Deg0;
    if (value == "90") return Rotation::Deg90;
    if (value == "180") return Rotation::Deg180;
    if (value == "270") return Rotation::Deg270;
    if (value == "auto") return Rotation::Auto;
    throw std::runtime_error("rotation must be 0, 90, 180, 270, or auto");
}

ErrorCorrection parseEcc(std::string value) {
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (value == "L" || value == "LOW") return ErrorCorrection::Low;
    if (value == "M" || value == "MEDIUM") return ErrorCorrection::Medium;
    if (value == "Q" || value == "QUARTILE") return ErrorCorrection::Quartile;
    if (value == "H" || value == "HIGH") return ErrorCorrection::High;
    throw std::runtime_error("ecc must be L, M, Q, or H");
}

bool logicalModule(const BarcodeResult& symbol, int x, int y) {
    if (symbol.linear) {
        return x >= 0 && x < symbol.matrix.width() && y >= 0 && y < 48 &&
               symbol.matrix.get(static_cast<uint16_t>(x), 0);
    }
    return x >= 0 && y >= 0 && x < symbol.matrix.width() && y < symbol.matrix.height() &&
           symbol.matrix.get(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

bool rotatedModule(const BarcodeResult& symbol, Rotation rotation, int x, int y) {
    const int width = symbol.matrix.width();
    const int height = symbol.linear ? 48 : symbol.matrix.height();
    switch (rotation) {
        case Rotation::Deg0: return logicalModule(symbol, x, y);
        case Rotation::Deg90: return logicalModule(symbol, y, height - 1 - x);
        case Rotation::Deg180: return logicalModule(symbol, width - 1 - x, height - 1 - y);
        case Rotation::Deg270: return logicalModule(symbol, width - 1 - y, x);
        case Rotation::Auto: return logicalModule(symbol, x, y);
    }
    return false;
}

void writePbm(const std::string& path,
              const BarcodeResult& symbol,
              int quiet,
              int scale,
              Rotation rotation,
              bool invert) {
    int contentWidth = symbol.matrix.width();
    int contentHeight = symbol.linear ? 48 : symbol.matrix.height();
    if (rotation == Rotation::Deg90 || rotation == Rotation::Deg270) {
        std::swap(contentWidth, contentHeight);
    }
    const int width = (contentWidth + 2 * quiet) * scale;
    const int height = (contentHeight + 2 * quiet) * scale;
    const int rowBytes = (width + 7) / 8;
    std::vector<uint8_t> pixels(static_cast<std::size_t>(rowBytes * height), 0U);

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const int x = px / scale - quiet;
            const int y = py / scale - quiet;
            bool black = x >= 0 && y >= 0 && x < contentWidth && y < contentHeight &&
                         rotatedModule(symbol, rotation, x, y);
            if (invert) black = !black;
            if (black) {
                pixels[static_cast<std::size_t>(py * rowBytes + px / 8)] |=
                    static_cast<uint8_t>(1U << (7U - static_cast<unsigned>(px & 7)));
            }
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create output: " + path);
    out << "P4\n" << width << ' ' << height << "\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4U];
                    out += hex[c & 15U];
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parseArgs(argc, argv);
        if (args.count("help") || !args.count("type")) {
            std::cerr << "Usage: barcode_native --type TYPE --data TEXT --output symbol.pbm "
                         "[--scale 8] [--quiet N] [--rotation 0] [--rect] [--ecc M]\n";
            return args.count("help") ? 0 : 2;
        }

        BarcodeSpec spec;
        if (!tryParseSymbology(get(args, "type"), spec.type)) {
            throw std::runtime_error("unknown symbology: " + get(args, "type"));
        }
        spec.data = get(args, "data");
        spec.ecc = parseEcc(get(args, "ecc", "M"));
        spec.dataMatrixRectangular = parseBool(get(args, "rect", "false"));
        spec.checksum = parseBool(get(args, "checksum", "true"));
        spec.qrMinVersion = static_cast<uint8_t>(getInt(args, "qr-min-version", 1));
        spec.qrMaxVersion = static_cast<uint8_t>(getInt(args, "qr-max-version", 20));
        spec.aztecSecurityPercent = static_cast<uint8_t>(getInt(args, "aztec-security", 23));
        spec.aztecMinLayers = static_cast<uint8_t>(getInt(args, "aztec-layers", 1));

        const auto result = encode(spec);
        if (!result.ok) {
            std::cerr << result.error << '\n';
            return 3;
        }

        const int quiet = getInt(args, "quiet", result.defaultQuietZone);
        const int scale = getInt(args, "scale", 8);
        const Rotation rotation = parseRotation(get(args, "rotation", "0"));
        if (quiet < 0 || scale < 1 || rotation == Rotation::Auto) {
            throw std::runtime_error("quiet must be non-negative, scale positive, and rotation explicit");
        }
        const std::string output = get(args, "output", "symbol.pbm");
        writePbm(output, result, quiet, scale, rotation, parseBool(get(args, "invert", "false")));

        std::cout << "{\"ok\":true,\"type\":\"" << toString(spec.type)
                  << "\",\"width_modules\":" << result.matrix.width()
                  << ",\"height_modules\":" << (result.linear ? 48 : result.matrix.height())
                  << ",\"linear\":" << (result.linear ? "true" : "false")
                  << ",\"quiet_modules\":" << quiet
                  << ",\"normalized_data\":\"" << jsonEscape(result.normalizedData)
                  << "\",\"output\":\"" << jsonEscape(output) << "\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
