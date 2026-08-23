#include "TrustCrypto.h"

#include <esp_random.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace esplink {

namespace {
mbedtls_ctr_drbg_context g_ctrDrbg;

// "Is the shared DRBG seeded" lives at file scope alongside the context it describes, NOT as a
// per-instance member. Several TrustCrypto instances share this one context (main.cpp's boot
// self-test, EspNowEndpoint's, and in gateway mode GatewayRelay's). With a per-instance flag, a
// later begin() would mbedtls_ctr_drbg_init() -- i.e. zero -- a context an earlier, still-live
// instance believed was ready, and a failed re-seed would leave that instance calling
// mbedtls_ctr_drbg_random() on zeroed state.
bool g_ctrDrbgReady = false;

int espRandomCallback(void*, unsigned char* output, std::size_t length) {
    esp_fill_random(output, length);
    return 0;
}

bool mpiToFixedBytes(const mbedtls_mpi& value, uint8_t* out, std::size_t outLength) {
    return mbedtls_mpi_write_binary(&value, out, outLength) == 0;
}

// HKDF-SHA256 (RFC 5869) built from mbedtls_md's HMAC, since mbedtls_hkdf.h is not guaranteed
// present on every mbedTLS version this project has built against.
bool hkdfSha256(const uint8_t* salt, std::size_t saltLength, const uint8_t* ikm, std::size_t ikmLength,
               const uint8_t* info, std::size_t infoLength, uint8_t* out, std::size_t outLength) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr) return false;
    constexpr std::size_t kHashLen = 32;

    uint8_t prk[kHashLen];
    if (mbedtls_md_hmac(mdInfo, salt, saltLength, ikm, ikmLength, prk) != 0) return false;

    uint8_t previous[kHashLen] = {};
    std::size_t previousLength = 0;
    std::size_t produced = 0;
    uint8_t counter = 1;
    while (produced < outLength) {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        if (mbedtls_md_setup(&ctx, mdInfo, 1) != 0 || mbedtls_md_hmac_starts(&ctx, prk, kHashLen) != 0 ||
            (previousLength > 0 && mbedtls_md_hmac_update(&ctx, previous, previousLength) != 0) ||
            mbedtls_md_hmac_update(&ctx, info, infoLength) != 0 ||
            mbedtls_md_hmac_update(&ctx, &counter, 1) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }
        uint8_t block[kHashLen];
        if (mbedtls_md_hmac_finish(&ctx, block) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }
        mbedtls_md_free(&ctx);

        const std::size_t take = std::min(kHashLen, outLength - produced);
        std::memcpy(out + produced, block, take);
        std::memcpy(previous, block, kHashLen);
        previousLength = kHashLen;
        produced += take;
        ++counter;
    }
    return true;
}
}  // namespace

bool TrustCrypto::begin(std::string& error) {
    if (g_ctrDrbgReady) {
        // The shared context is already seeded and in use by another instance, so it must never
        // be re-init'd (that would zero it out from under whoever is holding it). Mix in fresh
        // entropy instead: the first begin() of a boot is main.cpp's self-test, which runs before
        // Wi-Fi is up and therefore seeds from esp_random()'s weak pre-radio output. The later
        // calls -- EspNowEndpoint's, and GatewayRelay's in gateway mode -- happen after
        // WiFi.mode(WIFI_STA)/esp_now_init(), so reseeding here is what actually gets true
        // hardware entropy into the DRBG before any long-lived identity key is generated from it.
        //
        // A reseed failure is deliberately not fatal and not reported: the DRBG remains fully
        // seeded and usable either way, just with older entropy, so there is no state in which a
        // caller proceeds over an unusable context.
        mbedtls_ctr_drbg_reseed(&g_ctrDrbg, nullptr, 0);
        return true;
    }

    mbedtls_ctr_drbg_init(&g_ctrDrbg);
    const unsigned char customSeed[1] = {0};
    if (mbedtls_ctr_drbg_seed(&g_ctrDrbg, espRandomCallback, nullptr, customSeed, sizeof(customSeed)) != 0) {
        error = "mbedtls_ctr_drbg_seed failed";
        return false;
    }
    g_ctrDrbgReady = true;
    return true;
}

bool TrustCrypto::generateKeyPair(TrustKeyPair& out) const {
    if (!g_ctrDrbgReady) return false;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi privateScalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&privateScalar);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_gen_keypair(&group, &privateScalar, &point, mbedtls_ctr_drbg_random, &g_ctrDrbg) == 0 &&
              mpiToFixedBytes(privateScalar, out.privateKey.data(), out.privateKey.size());
    std::size_t written = 0;
    if (ok) {
        ok = mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &written,
                                            out.publicKey.data(), out.publicKey.size()) == 0 &&
             written == out.publicKey.size();
    }

    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::generateNonce(TrustNonce& out) const {
    if (!g_ctrDrbgReady) return false;
    return mbedtls_ctr_drbg_random(&g_ctrDrbg, out.data(), out.size()) == 0;
}

bool TrustCrypto::sign(const TrustPrivateKey& privateKey, const uint8_t* message, std::size_t messageLength,
                      TrustSignature& outSignature) const {
    if (!g_ctrDrbgReady) return false;
    TrustHash digest{};
    if (!sha256(message, messageLength, digest)) return false;

    mbedtls_ecp_group group;
    mbedtls_mpi privateScalar, r, s;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&privateScalar);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_mpi_read_binary(&privateScalar, privateKey.data(), privateKey.size()) == 0 &&
              mbedtls_ecdsa_sign(&group, &r, &s, &privateScalar, digest.data(), digest.size(),
                                 mbedtls_ctr_drbg_random, &g_ctrDrbg) == 0 &&
              mpiToFixedBytes(r, outSignature.data(), 32) && mpiToFixedBytes(s, outSignature.data() + 32, 32);

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::verify(const TrustPublicKey& publicKey, const uint8_t* message, std::size_t messageLength,
                        const TrustSignature& signature) const {
    TrustHash digest{};
    if (!sha256(message, messageLength, digest)) return false;

    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&group, &point, publicKey.data(), publicKey.size()) == 0 &&
              mbedtls_mpi_read_binary(&r, signature.data(), 32) == 0 &&
              mbedtls_mpi_read_binary(&s, signature.data() + 32, 32) == 0 &&
              mbedtls_ecdsa_verify(&group, digest.data(), digest.size(), &point, &r, &s) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::sha256(const uint8_t* data, std::size_t length, TrustHash& out) const {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mdInfo != nullptr && mbedtls_md(mdInfo, data, length, out.data()) == 0;
}

bool TrustCrypto::deriveSessionKeys(const TrustPrivateKey& ourEphemeralPrivateKey,
                                   const TrustPublicKey& /*ourEphemeralPublicKey*/,
                                   const TrustPublicKey& ourStaticPublicKey, const TrustNonce& ourNonce,
                                   const TrustPublicKey& peerEphemeralPublicKey,
                                   const TrustPublicKey& peerStaticPublicKey, const TrustNonce& peerNonce,
                                   TrustDerivedKeys& out) const {
    if (!g_ctrDrbgReady) return false;

    mbedtls_ecp_group group;
    mbedtls_ecp_point peerPoint, sharedPoint;
    mbedtls_mpi privateScalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&peerPoint);
    mbedtls_ecp_point_init(&sharedPoint);
    mbedtls_mpi_init(&privateScalar);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&group, &peerPoint, peerEphemeralPublicKey.data(),
                                            peerEphemeralPublicKey.size()) == 0 &&
              mbedtls_mpi_read_binary(&privateScalar, ourEphemeralPrivateKey.data(),
                                      ourEphemeralPrivateKey.size()) == 0 &&
              mbedtls_ecp_mul(&group, &sharedPoint, &privateScalar, &peerPoint, mbedtls_ctr_drbg_random,
                              &g_ctrDrbg) == 0;

    std::array<uint8_t, 32> sharedSecretX{};
    if (ok) ok = mpiToFixedBytes(sharedPoint.X, sharedSecretX.data(), sharedSecretX.size());

    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&sharedPoint);
    mbedtls_ecp_point_free(&peerPoint);
    mbedtls_ecp_group_free(&group);
    if (!ok) return false;

    // Reject identical static public keys outright: this is a red-flag case (a reflection
    // attempt or a cloned identity, not a normal pairing between two distinct devices), and the
    // memcmp-based canonicalization below returns false for both sides on equality, which would
    // otherwise make the two sides hash nonces in opposite order and silently derive different
    // keys instead of failing closed.
    if (std::memcmp(ourStaticPublicKey.data(), peerStaticPublicKey.data(), kTrustPublicKeyBytes) == 0) {
        return false;
    }

    // Canonicalize by comparing the two static public keys so both sides hash identically
    // regardless of which one is "ours" -- see the ITrustCrypto::deriveSessionKeys contract.
    const bool ourStaticIsSmaller =
        std::memcmp(ourStaticPublicKey.data(), peerStaticPublicKey.data(), kTrustPublicKeyBytes) < 0;
    const TrustPublicKey& firstStatic = ourStaticIsSmaller ? ourStaticPublicKey : peerStaticPublicKey;
    const TrustPublicKey& secondStatic = ourStaticIsSmaller ? peerStaticPublicKey : ourStaticPublicKey;
    const TrustNonce& firstNonce = ourStaticIsSmaller ? ourNonce : peerNonce;
    const TrustNonce& secondNonce = ourStaticIsSmaller ? peerNonce : ourNonce;

    std::array<uint8_t, kTrustNonceBytes * 2> salt{};
    std::memcpy(salt.data(), firstNonce.data(), kTrustNonceBytes);
    std::memcpy(salt.data() + kTrustNonceBytes, secondNonce.data(), kTrustNonceBytes);

    static constexpr char kInfoPrefix[] = "esbg-trust-v1";
    std::array<uint8_t, sizeof(kInfoPrefix) - 1 + kTrustPublicKeyBytes * 2> info{};
    std::size_t offset = 0;
    std::memcpy(info.data(), kInfoPrefix, sizeof(kInfoPrefix) - 1);
    offset += sizeof(kInfoPrefix) - 1;
    std::memcpy(info.data() + offset, firstStatic.data(), kTrustPublicKeyBytes);
    offset += kTrustPublicKeyBytes;
    std::memcpy(info.data() + offset, secondStatic.data(), kTrustPublicKeyBytes);

    // 16 bytes LMK + 4 bytes numeric-code seed + 32 bytes transcript hash = 52 bytes.
    std::array<uint8_t, kTrustLmkBytes + 4 + kTrustHashBytes> hkdfOutput{};
    if (!hkdfSha256(salt.data(), salt.size(), sharedSecretX.data(), sharedSecretX.size(), info.data(), info.size(),
                    hkdfOutput.data(), hkdfOutput.size())) {
        return false;
    }

    std::memcpy(out.lmk.data(), hkdfOutput.data(), kTrustLmkBytes);
    const uint8_t* codeBytes = hkdfOutput.data() + kTrustLmkBytes;
    out.numericCode = (static_cast<uint32_t>(codeBytes[0]) << 24 | static_cast<uint32_t>(codeBytes[1]) << 16 |
                       static_cast<uint32_t>(codeBytes[2]) << 8 | codeBytes[3]) %
                      1000000u;
    std::memcpy(out.transcriptHash.data(), hkdfOutput.data() + kTrustLmkBytes + 4, kTrustHashBytes);
    return true;
}

bool TrustCrypto::selfTest(std::string& error) {
    TrustKeyPair alice, bob;
    if (!generateKeyPair(alice) || !generateKeyPair(bob)) {
        error = "keygen failed";
        return false;
    }
    const uint8_t message[] = {1, 2, 3, 4, 5};
    TrustSignature signature{};
    if (!sign(alice.privateKey, message, sizeof(message), signature)) {
        error = "sign failed";
        return false;
    }
    if (!verify(alice.publicKey, message, sizeof(message), signature)) {
        error = "verify of a valid signature failed";
        return false;
    }
    if (verify(bob.publicKey, message, sizeof(message), signature)) {
        error = "verify accepted a signature from the wrong key";
        return false;
    }

    TrustNonce nonceA{}, nonceB{};
    if (!generateNonce(nonceA) || !generateNonce(nonceB)) {
        error = "nonce generation failed";
        return false;
    }
    TrustKeyPair ephemeralA, ephemeralB;
    if (!generateKeyPair(ephemeralA) || !generateKeyPair(ephemeralB)) {
        error = "ephemeral keygen failed";
        return false;
    }
    TrustDerivedKeys derivedA, derivedB;
    if (!deriveSessionKeys(ephemeralA.privateKey, ephemeralA.publicKey, alice.publicKey, nonceA, ephemeralB.publicKey,
                          bob.publicKey, nonceB, derivedA) ||
        !deriveSessionKeys(ephemeralB.privateKey, ephemeralB.publicKey, bob.publicKey, nonceB, ephemeralA.publicKey,
                          alice.publicKey, nonceA, derivedB)) {
        error = "deriveSessionKeys failed";
        return false;
    }
    if (derivedA.lmk != derivedB.lmk || derivedA.numericCode != derivedB.numericCode) {
        error = "deriveSessionKeys is not symmetric between the two sides";
        return false;
    }
    if (derivedA.transcriptHash != derivedB.transcriptHash) {
        error = "deriveSessionKeys transcriptHash is not symmetric between the two sides";
        return false;
    }
    return true;
}

}  // namespace esplink
