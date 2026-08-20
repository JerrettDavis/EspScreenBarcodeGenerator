#include "EspBarcodeCore.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#if __has_include(<qrcode.h>)
extern "C" {
#include <qrcode.h>
}
#define ESPBARCODE_HAS_RICMOO_QR 1
#else
#define ESPBARCODE_HAS_RICMOO_QR 0
#endif

namespace espbarcode {

BarcodeResult encodeQr(const BarcodeSpec& spec) {
#if ESPBARCODE_HAS_RICMOO_QR
    const uint8_t minimumVersion = std::clamp<uint8_t>(spec.qrMinVersion, 1, 40);
    const uint8_t maximumVersion = std::clamp<uint8_t>(spec.qrMaxVersion, minimumVersion, 40);
    uint8_t ecc = ECC_MEDIUM;
    switch (spec.ecc) {
        case ErrorCorrection::Low: ecc = ECC_LOW; break;
        case ErrorCorrection::Medium: ecc = ECC_MEDIUM; break;
        case ErrorCorrection::Quartile: ecc = ECC_QUARTILE; break;
        case ErrorCorrection::High: ecc = ECC_HIGH; break;
    }

    for (uint8_t version = minimumVersion; version <= maximumVersion; ++version) {
        std::vector<uint8_t> storage(qrcode_getBufferSize(version), 0U);
        QRCode qr{};
        const int8_t status = qrcode_initBytes(
            &qr,
            storage.data(),
            version,
            ecc,
            reinterpret_cast<uint8_t*>(const_cast<char*>(spec.data.data())),
            static_cast<uint16_t>(spec.data.size()));
        if (status != 0) {
            if (version == maximumVersion) break;
            continue;
        }

        BitMatrix matrix(qr.size, qr.size);
        for (uint8_t y = 0; y < qr.size; ++y) {
            for (uint8_t x = 0; x < qr.size; ++x) {
                if (qrcode_getModule(&qr, x, y)) matrix.set(x, y);
            }
        }
        BarcodeResult result;
        result.ok = true;
        result.matrix = std::move(matrix);
        result.normalizedData = spec.data;
        result.linear = false;
        result.defaultQuietZone = 4;
        return result;
    }

    BarcodeResult failure;
    failure.error = "QR payload does not fit the selected version range and error-correction level";
    return failure;
#else
    (void)spec;
    BarcodeResult failure;
    failure.error = "QR encoder dependency is unavailable in this build (ricmoo/QRCode)";
    return failure;
#endif
}

}  // namespace espbarcode
