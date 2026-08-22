// A software emulation of navigator.serial PLUS the ESP firmware's wire protocol (NDJSON v1 from
// docs/PROTOCOL.md, and the EspLink v2 COBS/hop-frame/envelope subset from docs/PROTOCOL_V2.md), so the
// Gherkin/Playwright suite can exercise the whole controller app -- device discovery, generation,
// presets, gateway relay -- deterministically and without physical hardware. Installed as
// `window.__espFakeSerial` (see webSerial.js's `serialApi()`, which prefers it when present) before the
// Blazor app boots; the app never knows the difference, since this object satisfies the exact subset of
// the Web Serial API the app calls.
//
// A real E2E hardware pass (2 boards over real Web Serial) still needs a real browser + real devices;
// this harness exists so the app's logic — including the v2 gateway stack — has deterministic,
// CI-runnable coverage independent of that.

(function () {
    function crc32(bytes) {
        let crc = 0xFFFFFFFF;
        for (let i = 0; i < bytes.length; i++) {
            crc ^= bytes[i];
            for (let bit = 0; bit < 8; bit++) {
                const mask = -(crc & 1);
                crc = (crc >>> 1) ^ (0xEDB88320 & mask);
            }
        }
        return (crc ^ 0xFFFFFFFF) >>> 0;
    }

    function cobsEncode(data) {
        const output = [0];
        let codeIndex = 0;
        let code = 1;
        for (let i = 0; i < data.length; i++) {
            if (data[i] === 0) {
                output[codeIndex] = code;
                codeIndex = output.length;
                output.push(0);
                code = 1;
            } else {
                output.push(data[i]);
                code++;
                if (code === 0xff) {
                    output[codeIndex] = code;
                    codeIndex = output.length;
                    output.push(0);
                    code = 1;
                }
            }
        }
        output[codeIndex] = code;
        return new Uint8Array(output);
    }

    function cobsDecode(data) {
        const output = [];
        let read = 0;
        while (read < data.length) {
            const code = data[read];
            if (code === 0) return null;
            read++;
            const blockLen = code - 1;
            if (read + blockLen > data.length) return null;
            for (let i = 0; i < blockLen; i++) output.push(data[read + i]);
            read += blockLen;
            if (code !== 255 && read < data.length) output.push(0);
        }
        return new Uint8Array(output);
    }

    function encodeHopFrame(opts) {
        const headerSize = 32;
        const payload = opts.payload;
        const frame = new Uint8Array(headerSize + payload.length + 4);
        const view = new DataView(frame.buffer);
        frame[0] = 0x45; frame[1] = 0x4c; // 'E','L'
        frame[2] = 2; frame[3] = 0;
        frame[4] = opts.frameType ?? 0;
        frame[5] = 0;
        frame[6] = opts.trafficClass ?? 0;
        frame[7] = opts.profileId ?? 4;
        view.setUint16(8, opts.routeId ?? 0, true);
        view.setUint16(10, headerSize, true);
        view.setUint32(12, opts.linkSessionId >>> 0, true);
        view.setUint32(16, opts.linkMessageId >>> 0, true);
        view.setUint32(20, (opts.linkCorrelationId ?? 0) >>> 0, true);
        view.setUint16(24, opts.fragmentIndex ?? 0, true);
        view.setUint16(26, opts.fragmentCount ?? 1, true);
        view.setUint16(28, payload.length, true);
        view.setUint16(30, 0, true);
        frame.set(payload, headerSize);
        const crc = crc32(frame.subarray(0, headerSize + payload.length));
        view.setUint32(headerSize + payload.length, crc, true);
        return frame;
    }

    function decodeHopFrame(bytes) {
        if (bytes.length < 36 || bytes[0] !== 0x45 || bytes[1] !== 0x4c || bytes[2] !== 2) return null;
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (view.getUint16(10, true) !== 32) return null;
        const payloadLength = view.getUint16(28, true);
        if (view.getUint16(30, true) !== 0) return null;
        const fragmentIndex = view.getUint16(24, true);
        const fragmentCount = view.getUint16(26, true);
        if (fragmentCount === 0 || fragmentIndex >= fragmentCount) return null;
        const rawLength = 32 + payloadLength + 4;
        if (bytes.length < rawLength) return null;
        const expectedCrc = view.getUint32(32 + payloadLength, true);
        if (crc32(bytes.subarray(0, 32 + payloadLength)) !== expectedCrc) return null;
        return {
            linkSessionId: view.getUint32(12, true),
            linkMessageId: view.getUint32(16, true),
            payload: bytes.slice(32, 32 + payloadLength),
        };
    }

    function encodeEnvelope(opts) {
        const headerSize = 32;
        const body = opts.body;
        const message = new Uint8Array(headerSize + body.length);
        const view = new DataView(message.buffer);
        message[0] = 0x45; message[1] = 0x4d; // 'E','M'
        message[2] = 2; message[3] = 0;
        message[4] = opts.kind ?? 1;
        message[5] = 0;
        message[6] = opts.serviceId ?? 0;
        message[7] = opts.codecId ?? 0;
        view.setUint32(8, opts.controlSessionId >>> 0, true);
        view.setUint32(12, body.length, true);
        view.setBigUint64(16, BigInt(opts.operationId), true);
        view.setBigUint64(24, BigInt(opts.correlationId), true);
        message.set(body, headerSize);
        return message;
    }

    function decodeEnvelope(bytes) {
        if (bytes.length < 32 || bytes[0] !== 0x45 || bytes[1] !== 0x4d || bytes[2] !== 2) return null;
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const bodyLength = view.getUint32(12, true);
        if (bytes.length - 32 < bodyLength) return null;
        return {
            kind: bytes[4],
            operationId: Number(view.getBigUint64(16, true)),
            body: bytes.slice(32, 32 + bodyLength),
        };
    }

    function packBits(bits) {
        const out = new Uint8Array(Math.ceil(bits.length / 8));
        for (let i = 0; i < bits.length; i++) {
            if (bits[i]) out[i >> 3] |= 0x80 >> (i & 7);
        }
        return out;
    }

    function bytesToBase64(bytes) {
        let binary = "";
        for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
        return btoa(binary);
    }

    const LINEAR_TYPES = new Set(["code128", "gs1-128", "code39", "upca", "ean13", "ean8", "itf", "itf14", "codabar", "msi"]);

    class FakePort {
        constructor(config) {
            this.config = config || {};
            this.mode = "v1";
            this.presets = {};
            this.current = null;
            this.backlightOn = true;
            this._v1Bytes = [];
            this._relayBlock = [];
            this._buildStreams();
        }

        _buildStreams() {
            const self = this;
            this.readable = new ReadableStream({
                start(controller) { self._controller = controller; },
                cancel() { self._controller = null; },
            });
            this.writable = new WritableStream({
                write(chunk) { self._onHostWrite(chunk); },
            });
        }

        getInfo() { return { usbVendorId: 0x303a, usbProductId: 0x1001 }; }
        async open() { /* streams already live */ }
        async close() { this._controller = null; this._buildStreams(); }
        async forget() { this.config.authorized = false; }

        _emit(bytes) { if (this._controller) this._controller.enqueue(bytes); }
        _emitLine(obj) { this._emit(new TextEncoder().encode(JSON.stringify(obj) + "\n")); }

        _onHostWrite(chunk) {
            for (const b of chunk) {
                if (this.mode === "v1") {
                    if (b === 0x0a) {
                        const line = new TextDecoder().decode(new Uint8Array(this._v1Bytes));
                        this._v1Bytes = [];
                        this._handleV1Line(line);
                    } else if (b !== 0x0d) {
                        this._v1Bytes.push(b);
                    }
                } else if (b === 0x00) {
                    if (this._relayBlock.length) this._handleRelayBlock(new Uint8Array(this._relayBlock));
                    this._relayBlock = [];
                } else {
                    this._relayBlock.push(b);
                }
            }
        }

        _mergeSpec(req) {
            const type = req.type ?? this.current?.type ?? "qr";
            const data = req.data ?? this.current?.data ?? "";
            const linear = LINEAR_TYPES.has(type);
            const size = Math.max(9, Math.min(41, 9 + (data.length % 12) * 2));
            const width = linear ? Math.max(20, data.length * 3) : size;
            const height = linear ? 1 : size;
            let seed = 0;
            for (const ch of data) seed = (seed * 31 + ch.charCodeAt(0)) >>> 0;
            const bits = [];
            for (let i = 0; i < width * height; i++) {
                seed = (seed * 1103515245 + 12345) >>> 0;
                bits.push((seed >>> 16) & 1);
            }
            return { type, data, quiet: req.quiet ?? 4, save_as: req.save_as, matrix: { width, height, linear, bits } };
        }

        _handleV1Line(line) {
            if (!line) return;
            let req;
            try { req = JSON.parse(line); } catch { return; }
            const id = req.id;
            const cmd = req.cmd;
            const reply = (fields) => this._emitLine({ id, ok: true, cmd, ...fields });
            const fail = (code, message) => this._emitLine({ id, ok: false, cmd, error: { code, message } });

            switch (cmd) {
                case "hello":
                case "ping":
                    reply({
                        device: this.config.device ?? "EspScreenBarcodeGenerator",
                        firmware: this.config.firmware ?? "0.1.0-fake",
                        protocol: "1.0",
                        transport: "usb-serial",
                        screen: { width: this.config.screenWidth ?? 320, height: this.config.screenHeight ?? 480 },
                    });
                    break;
                case "status":
                    reply({
                        barcode_visible: this.backlightOn && !!this.current,
                        has_current: !!this.current,
                        current_raw: false,
                        status: this.current ? "displaying" : "idle",
                        free_heap: 182000,
                    });
                    break;
                case "generate": {
                    const spec = this._mergeSpec(req);
                    this.current = spec;
                    if (spec.save_as) this.presets[spec.save_as] = spec;
                    reply({
                        type: spec.type, width: spec.matrix.width, height: spec.matrix.height,
                        linear: spec.matrix.linear, quiet: spec.quiet, displayed: req.display !== false,
                        normalized_data: spec.data,
                    });
                    break;
                }
                case "display": {
                    if (req.name) {
                        const preset = this.presets[req.name];
                        if (!preset) { fail("display_failed", "preset not found"); break; }
                        this.current = preset;
                    }
                    if (!this.current) { fail("display_failed", "no current symbol"); break; }
                    reply({});
                    break;
                }
                case "close":
                case "home":
                    reply({});
                    break;
                case "save":
                    if (!this.current) { fail("save_failed", "no current symbol"); break; }
                    if (!req.name) { fail("missing_name", "name required"); break; }
                    this.presets[req.name] = this.current;
                    reply({});
                    break;
                case "load": {
                    const preset = this.presets[req.name];
                    if (!preset) { fail("load_failed", "preset not found"); break; }
                    this.current = preset;
                    if (req.display) { /* already "displayed" in this fake */ }
                    reply({});
                    break;
                }
                case "delete":
                    if (!this.presets[req.name]) { fail("delete_failed", "preset not found"); break; }
                    delete this.presets[req.name];
                    reply({});
                    break;
                case "list":
                    reply({ presets: Object.keys(this.presets) });
                    break;
                case "backlight":
                    this.backlightOn = !!req.on;
                    reply({});
                    break;
                case "orientation":
                case "reboot":
                    reply({});
                    break;
                case "download":
                    this._handleDownload(id, req);
                    break;
                case "upgrade":
                    reply({ message: "switching to EspLink v2 COBS framing" });
                    break;
                case "gateway":
                    reply({ message: "entering gateway mode" });
                    this.mode = "relay";
                    this._relayBlock = [];
                    break;
                default:
                    fail("unknown_command", `unsupported: ${cmd}`);
            }
        }

        _handleDownload(id, req) {
            if (!this.current) {
                this._emitLine({ id, ok: false, cmd: "download", error: { code: "no_symbol", message: "no current symbol" } });
                return;
            }
            const { width, height, linear, bits } = this.current.matrix;
            const packed = packBits(bits);
            const crc = crc32(packed);
            this._emitLine({
                id, ok: true, cmd: "download", event: "download_begin", width, height, linear,
                quiet: this.current.quiet, rotation: "auto", invert: false, label: this.current.data,
                bytes: packed.length, encoding: "base64-packed-msb-first", crc32: crc,
            });
            const chunkBytes = req.chunk_bytes ?? 384;
            for (let offset = 0; offset < packed.length; offset += chunkBytes) {
                const chunk = packed.slice(offset, offset + chunkBytes);
                this._emitLine({ id, ok: true, cmd: "download", event: "download_chunk", offset, data: bytesToBase64(chunk) });
            }
            this._emitLine({ id, ok: true, cmd: "download", event: "download_end", bytes: packed.length, crc32: crc });
        }

        _handleRelayBlock(block) {
            const raw = cobsDecode(block);
            if (!raw) return;
            const frame = decodeHopFrame(raw);
            if (!frame) return;
            const envelope = decodeEnvelope(frame.payload);
            if (!envelope || envelope.kind !== 0) return;

            let wrapper;
            try { wrapper = JSON.parse(new TextDecoder().decode(envelope.body)); } catch { return; }
            const name = wrapper.name;
            const body = wrapper.body ?? {};

            let isError = false;
            let responseName = name;
            let responseBody = {};

            if (name === "system.hello" || name === "system.ping") {
                responseName = "system.welcome";
                this._gatewayControlSessionId = (this._gatewayControlSessionId ?? 0) + 1;
                responseBody = {
                    deviceId: "esbg-usb-v2-fake",
                    firmware: this.config.firmware ?? "0.1.0-fake",
                    selectedVersion: "2.0",
                    controlSessionId: this._gatewayControlSessionId,
                    carrier: { profile: "stream-standard", maxFrameBytes: 4096 },
                };
            } else if (name === "barcode.generate") {
                const spec = this._mergeSpec(body);
                this.current = spec;
                responseBody = {
                    type: spec.type, width: spec.matrix.width, height: spec.matrix.height,
                    linear: spec.matrix.linear, quiet: spec.quiet, displayed: body.display !== false,
                    normalized_data: spec.data,
                };
            } else {
                isError = true;
            }

            const respWrapper = isError
                ? { schema: "esbg.control/2.0", name: responseName, error: { code: "unknown_command", message: "command not supported over EspLink v2 this release" } }
                : { schema: "esbg.control/2.0", name: responseName, body: responseBody };

            const bodyBytes = new TextEncoder().encode(JSON.stringify(respWrapper));
            this._respOpCounter = (this._respOpCounter ?? 0) + 1;
            const envelopeBytes = encodeEnvelope({
                kind: isError ? 3 : 1, controlSessionId: this._gatewayControlSessionId ?? 0,
                operationId: this._respOpCounter, correlationId: envelope.operationId, body: bodyBytes,
            });
            this._respLinkMsgCounter = (this._respLinkMsgCounter ?? 0) + 1;
            const hop = encodeHopFrame({
                linkSessionId: frame.linkSessionId, linkMessageId: this._respLinkMsgCounter,
                fragmentIndex: 0, fragmentCount: 1, payload: envelopeBytes,
            });
            const cobs = cobsEncode(hop);
            const withDelimiter = new Uint8Array(cobs.length + 1);
            withDelimiter.set(cobs, 0);
            this._emit(withDelimiter);
        }
    }

    const registry = { ports: [], configured: false };

    function ensureConfigured() {
        if (registry.configured) return;
        registry.configured = true;
        const configs = window.__espFakeSerialConfig ?? [{ id: "fake-1", authorized: true }];
        for (const cfg of configs) registry.ports.push({ port: new FakePort(cfg), cfg });
    }

    window.__espFakeSerial = {
        async requestPort() {
            ensureConfigured();
            const next = registry.ports.find((p) => !p.cfg.__claimed);
            if (!next) throw new DOMException("No port selected by the user.", "NotFoundError");
            next.cfg.__claimed = true;
            next.cfg.authorized = true;
            return next.port;
        },
        async getPorts() {
            ensureConfigured();
            return registry.ports.filter((p) => p.cfg.authorized).map((p) => p.port);
        },
    };
})();
