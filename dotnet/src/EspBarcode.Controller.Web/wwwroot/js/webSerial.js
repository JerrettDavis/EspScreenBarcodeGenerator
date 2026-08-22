// Thin wrapper around the Web Serial API (https://developer.mozilla.org/docs/Web/API/Web_Serial_API).
// Ports are tracked by an opaque string id so C# never has to hold a JS object reference directly.
// A test harness can install `window.__espFakeSerial` (see fakeSerial.js) before this module loads
// its first `navigator.serial` access; when present it is used instead of the real API, so the whole
// stack above this file (byte streams, NDJSON v1 client, EspLink v2 session) runs unmodified in
// automated tests with no physical device attached.

const ports = new Map();
let nextId = 1;

function serialApi() {
    return window.__espFakeSerial ?? navigator.serial;
}

export function isSupported() {
    return serialApi() != null;
}

export async function requestPort() {
    const port = await serialApi().requestPort();
    return registerPort(port);
}

export async function getAuthorizedPorts() {
    const list = await serialApi().getPorts();
    return list.map(p => registerPort(p));
}

function registerPort(port) {
    for (const [id, entry] of ports) {
        if (entry.port === port) return describePort(id, entry);
    }
    const id = "port-" + (nextId++);
    ports.set(id, { port, reader: null, writer: null });
    return describePort(id, ports.get(id));
}

function describePort(id, entry) {
    const info = typeof entry.port.getInfo === "function" ? entry.port.getInfo() : {};
    return {
        id,
        usbVendorId: info.usbVendorId ?? null,
        usbProductId: info.usbProductId ?? null,
    };
}

export async function open(id, baudRate) {
    const entry = mustGet(id);
    await entry.port.open({ baudRate });
}

// Pumps the port's readable stream and forwards each chunk to .NET as base64. Runs until the
// stream closes/errors (disconnect, port.close() from the other side) or stopReading() cancels it.
export async function startReading(id, dotNetRef) {
    const entry = mustGet(id);
    if (!entry.port.readable) return;
    entry.reader = entry.port.readable.getReader();
    try {
        while (true) {
            const { value, done } = await entry.reader.read();
            if (done) break;
            if (value && value.length) {
                await dotNetRef.invokeMethodAsync("OnSerialData", bytesToBase64(value));
            }
        }
    } catch {
        // Reader rejects on cancel()/disconnect; the finally below always reports closure.
    } finally {
        try { entry.reader.releaseLock(); } catch { /* already released */ }
        entry.reader = null;
        await dotNetRef.invokeMethodAsync("OnSerialClosed");
    }
}

export async function write(id, base64) {
    const entry = mustGet(id);
    if (!entry.port.writable) throw new Error("port is not writable");
    if (!entry.writer) entry.writer = entry.port.writable.getWriter();
    await entry.writer.write(base64ToBytes(base64));
}

export async function close(id) {
    const entry = ports.get(id);
    if (!entry) return;
    if (entry.reader) { try { await entry.reader.cancel(); } catch { /* ignore */ } }
    if (entry.writer) { try { entry.writer.releaseLock(); } catch { /* ignore */ } }
    try { await entry.port.close(); } catch { /* ignore */ }
    ports.delete(id);
}

/** Revokes the browser's grant for this port (used by tests to reset state; also user-facing "forget device"). */
export async function forget(id) {
    const entry = ports.get(id);
    if (entry && typeof entry.port.forget === "function") await entry.port.forget();
    ports.delete(id);
}

function mustGet(id) {
    const entry = ports.get(id);
    if (!entry) throw new Error(`unknown serial port id ${id}`);
    return entry;
}

function bytesToBase64(bytes) {
    let binary = "";
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    return btoa(binary);
}

function base64ToBytes(base64) {
    const binary = atob(base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
}
