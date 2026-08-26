// Test-only Web Bluetooth facade. It adapts fakeSerial's byte-accurate EspLink v2 emulator
// from COBS stream framing to the one-hop-frame-per-GATT-message contract used by BLE.
(function () {
    function cobsEncode(data) {
        const output = [0]; let codeIndex = 0; let code = 1;
        for (const b of data) {
            if (b === 0) { output[codeIndex] = code; codeIndex = output.length; output.push(0); code = 1; }
            else { output.push(b); if (++code === 0xff) { output[codeIndex] = code; codeIndex = output.length; output.push(0); code = 1; } }
        }
        output[codeIndex] = code; return new Uint8Array(output);
    }
    function cobsDecode(data) {
        const output = []; let read = 0;
        while (read < data.length) {
            const code = data[read++]; if (!code || read + code - 1 > data.length) return null;
            for (let i = 1; i < code; i++) output.push(data[read++]);
            if (code !== 255 && read < data.length) output.push(0);
        }
        return new Uint8Array(output);
    }
    function base64(bytes) { let binary = ''; for (const b of bytes) binary += String.fromCharCode(b); return btoa(binary); }
    function fromBase64(value) { const binary = atob(value); return Uint8Array.from(binary, c => c.charCodeAt(0)); }

    const links = new Map(); let next = 1;
    window.__espFakeBluetoothTelemetry = { activeWrites: 0, maxConcurrentWrites: 0 };
    window.__espFakeBluetooth = {
        async connect(dotNetRef) {
            const config = (window.__espFakeBluetoothConfig ?? [{ name: 'Lab Display', firmware: '0.2.0-fake' }])[links.size];
            if (!config) throw new DOMException('No Bluetooth device selected.', 'NotFoundError');
            const port = window.__espFakeTransportFactory(config); port.mode = 'relay';
            const id = `fake-ble-${next++}`; const reader = port.readable.getReader();
            const state = { port, reader, dotNetRef, block: [], closed: false }; links.set(id, state);
            (async () => {
                while (!state.closed) {
                    const { value, done } = await reader.read(); if (done) break;
                    for (const b of value) {
                        if (b === 0) { const raw = cobsDecode(new Uint8Array(state.block)); state.block = []; if (raw) await dotNetRef.invokeMethodAsync('OnBluetoothData', base64(raw)); }
                        else state.block.push(b);
                    }
                }
            })();
            return { id, name: config.name ?? 'ESP Barcode Display', maxFrameBytes: 4096 };
        },
        async write(id, encoded) {
            const state = links.get(id); if (!state) throw new Error('Bluetooth device is not connected');
            const telemetry = window.__espFakeBluetoothTelemetry;
            telemetry.activeWrites++;
            telemetry.maxConcurrentWrites = Math.max(telemetry.maxConcurrentWrites, telemetry.activeWrites);
            try {
                if (state.port.config.writeDelayMs) await new Promise(resolve => setTimeout(resolve, state.port.config.writeDelayMs));
                const framed = cobsEncode(fromBase64(encoded)); const bytes = new Uint8Array(framed.length + 1); bytes.set(framed); bytes[bytes.length - 1] = 0;
                state.port._onHostWrite(bytes);
            } finally {
                telemetry.activeWrites--;
            }
        },
        async disconnect(id) {
            const state = links.get(id); if (!state) return; state.closed = true; await state.reader.cancel(); links.delete(id);
        }
    };
})();
