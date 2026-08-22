# ESP-NOW Secure Pairing Design

## Goal

Today ESP-NOW traffic (both the direct client compatibility profile in
`EspNowEndpoint` and the USB↔ESP-NOW `GatewayRelay`) is fully open: one shared
unencrypted broadcast peer, "accepts any sender this session" (see the
existing comments in `src/EspNowEndpoint.cpp` and `src/GatewayRelay.cpp`).
This was an explicit, documented scope cut (`docs/PROTOCOL_V2.md` §10, and
the original multi-transport design plan's §7.7 "Trust and Pairing", which
already reserved `ServiceId::Trust = 6` and named a `trust.*` command
family without implementing any of it).

This spec closes that gap: a device can opt into "Secure Pairing" mode, in
which

- no unpaired peer can send commands to it or have commands sent to it,
- a new peer can only become trusted through an on-screen, human-confirmed
  pairing ceremony (no silent auto-trust), and
- the ceremony uses industry-standard asymmetric cryptography (ECDH key
  agreement + ECDSA signing, both P-256) rather than a shared PSK or
  bespoke crypto, so a passive eavesdropper cannot decrypt traffic and an
  active MITM present during pairing cannot slip in undetected.

## Scope

Applies to both ESP-NOW roles: `EspNowEndpoint` (direct client/compatibility
profile) and `GatewayRelay` (USB↔ESP-NOW bridge). Out of scope: BLE, Wi-Fi
Direct, and Serial transports — they have their own trust stories (BLE
already does OS-level pairing; Wi-Fi Direct has its own provisioning). Also
out of scope: read-only/administrator lease distinctions beyond simple
pairing (§7.7's "administrator lease" concept is not needed here — every
paired peer gets full control, matching today's single-controller model).

## 1. Crypto primitives & handshake

**Identity.** On first boot, each device generates a persistent **ECDSA
P-256** keypair via mbedTLS (bundled with the ESP32 Arduino core — no new
library dependency for the on-device build). The private key is stored in
NVS (`Preferences`, a dedicated `"trust"` namespace, never written to the
LittleFS JSON trust-record file). The public key's SHA-256 hash, truncated
and hex-formatted (e.g. `A3F9-21C4`), is the fingerprint shown on-screen and
used as the peer's stable identity — independent of MAC address, so a
factory-reset-and-re-flash produces a new identity that must be re-paired,
which is correct.

One curve family serves both roles: the static identity key signs (ECDSA),
and a fresh keypair on the same curve is generated per handshake for ECDH.

**Handshake message 1 (`trust.pair.begin`, broadcast, plaintext):**
```
{ staticPubKey, ephemeralPubKey, nonce, signature = ECDSA_sign(staticPriv, ephemeralPubKey || nonce) }
```
Only the initiating side needs a human to have pressed "Pair new device"
and picked a target from the discovered-peers list (§4). The target does
**not** need to have entered any pairing mode itself — receiving a
`trust.pair.begin` addressed to it (by MAC, from the discovery side-channel
it already answers) is enough to make it reply with its own message of the
same shape and pop up its approval overlay unsolicited. Both sides still
independently tap Confirm before anything is trusted (§ below) — only the
*initiation* is one-sided, not the *approval*.

**Verification (both sides):** check the signature against the *claimed*
`staticPubKey` — this only proves the sender holds the matching private key
(self-consistency), not that the key should be trusted. Trust comes from
step 4.

**Key derivation:** both sides compute the ECDH shared secret from the two
`ephemeralPubKey`s, then run HKDF-SHA256 (info string `"esbg-trust-v1"`)
over `(sharedSecret, both nonces, both static pubkeys)` to derive two
independent outputs:
- a 16-byte **LMK** — installed as the `esp_now_peer_info_t::lmk` for this
  peer's future encrypted unicast traffic;
- a **6-digit numeric code** (0–999999, taken from a disjoint slice of the
  HKDF output so it reveals nothing about the LMK) for numeric-comparison
  display.

**On-screen approval (`trust.pair.confirm`):** both screens show the
peer's fingerprint and the 6-digit code. The user visually compares the
codes shown on both physical screens and taps **Confirm** on each device
independently, within a 60-second window. Each `Confirm` tap sends
`trust.pair.confirm` to the peer; a device only commits the trust record
once it has both sent and received a confirm. A `Deny` tap, or a 60s
timeout with no confirm, sends/implies `trust.pair.cancel` and the derived
keys are discarded — nothing is persisted for a cancelled attempt.

**Trust record (persisted on success):**
```
{ peerStaticPubKey, peerMac, fingerprint, role /* "gateway"|"client" */, routeId, pairedAtMs, label }
```

**Reconnect (already-trusted peer):** identical message-1 exchange, but
signature verification is checked against the *stored* trusted
`staticPubKey` for that fingerprint (matched by MAC, then confirmed by
signature — MAC alone is never trust, only a lookup hint). If valid, the
freshly derived LMK is installed silently — no human step, no repeated
prompt. This is the same shape as SSH host-key reconnects: trust is
established once, then every session gets a fresh authenticated key
without re-bothering the user. `trust.controller.forget` deletes a trust
record, which forces the next contact with that fingerprint back through
full pairing.

## 2. Wire format & enforcement

Commands ride `ServiceId::Trust = 6` (already reserved in
`ConnectivityTypes.h`), using the same `MessageEnvelope`/`HopFrame`/JSON
machinery every other service uses — no new framing:

| Command | Kind | Purpose |
|---|---|---|
| `trust.pair.begin` | Event | Handshake message 1 (see above); also how a device announces "my pairing window is open" |
| `trust.pair.confirm` | Event | Human's approval tap; carries nothing beyond the peer's fingerprint being confirmed |
| `trust.pair.cancel` | Event | Explicit deny or window timeout |
| `trust.controllers.list` | Command/Result | Local query: this device's own trust records (gateway's paired clients, or a client's paired gateways) |
| `trust.controller.forget` | Command/Result | Revoke one trust record by fingerprint |

**Enforcement**, gated by a per-device Settings toggle **"Secure Pairing"**
(default **off**):

- **Off (default):** unchanged from today — single unencrypted broadcast
  peer, accepts any sender. The existing hw-validated compatibility profile
  is untouched byte-for-byte.
- **On:** the device immediately stops accepting or sending plaintext
  application traffic (`barcode.*`, `device.*`, `gateway.*`) to/from any MAC
  not backed by a trust record. No grace period — flipping the toggle with
  unpaired peers already "connected" breaks them until paired. `trust.*`
  pairing messages and the existing `gateway.link.ping`/`pong` discovery
  side-channel remain on the open broadcast channel always (a brand-new
  peer has no key yet — that channel is how it gets discovered and paired).
  Once paired, that peer's traffic moves to **unicast**
  `esp_now_send(peerMac, ...)` using an `esp_now_add_peer` entry with
  `encrypt=true, lmk=<derived>`.

**Routing consequence for `GatewayRelay`.** It currently blind-broadcasts
every relayed frame (correct for the single-client case it's been
validated against). Multiple paired clients need per-client targeting,
which is exactly what `HopFrameHeader.routeId` was reserved for
("gateway-assigned peer routes `0x0001`–`0xFFFE` ... reserved by the design
but unimplemented" — `docs/PROTOCOL_V2.md`). This feature wires that up:
each trust record gets an assigned `routeId`, and `relayToEspNow` looks up
the target MAC from the trust store by `routeId` instead of broadcasting,
when Secure Pairing is on. When Secure Pairing is off, behavior is
unchanged (broadcast, `routeId` unused) — this keeps the un-paired,
single-client bench-validated path exactly as it is today.
`EspNowEndpoint` (direct/client mode) does not need routing: it is
inherently 1:1 with whichever single gateway/controller it is currently
paired with.

**Peer/key limits.** ESP-NOW's hardware AES-CCM peer encryption has a small
ceiling on concurrently-encrypted peers (`esp_now.h`'s
`ESP_NOW_MAX_ENCRYPT_PEER_NUM` for the targeted IDF release — to be
confirmed exactly during implementation, historically 6). The trust store's
max-record cap defaults to that constant. Pairing beyond the cap fails
cleanly with an on-screen message ("trust list full — forget a device
first") rather than silently evicting an existing record.

**Anti-replay.** The existing per-link `linkMessageId` monotonic counter
(already in `HopFrameHeader`) is enforced strictly increasing per encrypted
peer once paired; a replayed/duplicate frame is dropped.

## 3. Persistence

New `TrustStore` component (firmware, `lib/EspLinkCore` + a thin ESP32-side
adapter), mirroring the existing `DeviceConfigStore` pattern:
- `/config/trust.json` on LittleFS: the list of trust records (public
  data only — no private key material).
- The device's own static ECDSA keypair lives in NVS via `Preferences`
  under a dedicated namespace, generated once on first boot if absent.

`TrustStore` is a plain value-object-plus-persistence class, independent of
`EspNowEndpoint`/`GatewayRelay`, so it's unit-testable in the native build
without any ESP32/Arduino dependency (see Testing below).

## 4. On-device UI

Extends the existing touchscreen `View` state machine
(`BarcodeApplication.cpp`) the same way the Gateway stats screen and PING
button were added:

- **Settings screen:** new "Secure Pairing: On/Off" toggle, next to the
  existing dark/light theme toggle.
- **New "Trust" screen**, reachable from Settings or the Gateway screen:
  - **Pair new device** button: opens this device's pairing window (bounded,
    120s countdown shown), lists nearby not-yet-trusted peers discovered
    via the existing `gateway.link.ping`/`pong` side-channel, tap one to
    start the handshake.
  - **Pending approval** overlay: peer fingerprint + 6-digit code + big
    Confirm/Deny buttons + countdown.
  - **Trust list**: paired peers (fingerprint, label, last-seen), each with
    a **Forget** action.

## 5. .NET / Blazor integration

The gateway is USB-tethered to a PC, so the PC can drive pairing
initiation/management even though the confirm tap must stay physical:

- `GatewayLinkClient` gains `ListTrustedPeersAsync`, `BeginPairingAsync`,
  `ForgetPeerAsync`, `CancelPairingAsync` — forwarded to the gateway
  board's own local `trust.*` handlers over the existing USB channel
  (same "handled locally, not relayed to ESP-NOW" pattern
  `gateway.peers.list`/`gateway.ping.now` already use in
  `GatewayRelay::handleGatewayServiceFromUsb`).
- `Gateway.razor` gets a Trust section: trusted-peer list with Forget
  buttons, a "Pair new device" button that arms the gateway's pairing
  window, and **read-only** live status while a pairing is pending
  (peer fingerprint + the same 6-digit code, so the operator doesn't have
  to walk between two physical screens to compare — but there is no
  Approve button in the UI; the tap must happen on-device, on both ends).

## 6. Testing

- **Native unit tests** (`test/test_native`, Unity): HKDF/derivation
  correctness, sign/verify round-trips, trust-store persistence
  (save/load/cap-enforcement/forget), and the pairing state machine
  (begin → confirm/confirm → committed; begin → cancel/timeout →
  discarded) — all independent of ESP32 hardware. mbedTLS is a portable C
  library; the native PlatformIO env needs it added as a `lib_deps` entry
  (or a vendored subset) purely for the test build, since `env:native`
  doesn't otherwise get the ESP-IDF-bundled copy the on-device build gets.
- **BDD/E2E** (existing Reqnroll+Playwright suite): new scenarios for the
  Blazor Trust section — pair button arms pairing, trust list renders,
  forget removes a record — using the existing `fakeSerial.js` mock
  pattern from the Gateway feature tests.
- **Manual hardware validation checklist** (numeric-comparison pairing
  can't be automated across two physical touchscreens): full pair flow with
  code match confirmed on both screens; deny/timeout discards nothing;
  reconnect after reboot is silent (no re-prompt); toggling Secure Pairing
  on immediately locks out a previously-open unpaired peer; Forget forces
  re-pairing on next contact; trust-list-full behavior at the cap.

## Non-goals (explicitly deferred)

- Multi-controller "administrator lease" distinctions from the original
  §7.7 sketch — every paired peer gets equal control, matching today's
  model.
- Key rotation/expiry policies beyond forget-and-re-pair.
- Any transport other than ESP-NOW.
- Automated hardware-in-the-loop testing of the physical confirm taps.
