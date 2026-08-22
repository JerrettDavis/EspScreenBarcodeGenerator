// Draws a packed row-major, MSB-first module matrix (docs/PROTOCOL.md "Packing contract") onto a
// <canvas>, matching the firmware's own module-cell rendering pixel-for-pixel (module = one square).
export function renderMatrix(canvasId, width, height, invert, packedBase64) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || width <= 0 || height <= 0) return;

    const bytes = base64ToBytes(packedBase64);
    const scale = Math.max(1, Math.floor(280 / Math.max(width, height)));
    canvas.width = width * scale;
    canvas.height = height * scale;

    const ctx = canvas.getContext("2d");
    ctx.fillStyle = invert ? "#000000" : "#ffffff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = invert ? "#ffffff" : "#000000";

    let bitIndex = 0;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const byteIndex = bitIndex >> 3;
            const bitInByte = 7 - (bitIndex & 7);
            const bit = (bytes[byteIndex] >> bitInByte) & 1;
            if (bit) ctx.fillRect(x * scale, y * scale, scale, scale);
            bitIndex++;
        }
    }
}

function base64ToBytes(base64) {
    const binary = atob(base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
}
