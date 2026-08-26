const SERVICE = '6f6d7501-2e73-4a1a-9d3f-1c9b6f4e5a01';
const COMMAND = '6f6d7502-2e73-4a1a-9d3f-1c9b6f4e5a01';
const EVENT = '6f6d7503-2e73-4a1a-9d3f-1c9b6f4e5a01';
const links = new Map();
let nextId = 1;

function fakeApi() { return window.__espFakeBluetooth; }
export function isSupported() { return !!fakeApi() || !!navigator.bluetooth; }
export async function connect(dotNetRef) {
  if (fakeApi()) return await fakeApi().connect(dotNetRef);
  const device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE] }] });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE);
  const command = await service.getCharacteristic(COMMAND);
  const events = await service.getCharacteristic(EVENT);
  const id = `ble-${nextId++}`;
  const onValue = e => {
    const view = e.target.value;
    const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    let binary = ''; for (const b of bytes) binary += String.fromCharCode(b);
    dotNetRef.invokeMethodAsync('OnBluetoothData', btoa(binary));
  };
  const onDisconnect = () => dotNetRef.invokeMethodAsync('OnBluetoothClosed');
  events.addEventListener('characteristicvaluechanged', onValue);
  device.addEventListener('gattserverdisconnected', onDisconnect);
  await events.startNotifications();
  links.set(id, { device, command, events, onValue, onDisconnect });
  return { id, name: device.name || 'ESP Barcode Display', maxFrameBytes: 200 };
}
export async function write(id, base64) {
  if (fakeApi()) return await fakeApi().write(id, base64);
  const link = links.get(id); if (!link) throw new Error('Bluetooth device is not connected');
  const binary = atob(base64); const bytes = Uint8Array.from(binary, c => c.charCodeAt(0));
  await link.command.writeValueWithResponse(bytes);
}
export async function disconnect(id) {
  if (fakeApi()) return await fakeApi().disconnect(id);
  const link = links.get(id); if (!link) return;
  link.events.removeEventListener('characteristicvaluechanged', link.onValue);
  link.device.removeEventListener('gattserverdisconnected', link.onDisconnect);
  if (link.device.gatt.connected) link.device.gatt.disconnect();
  links.delete(id);
}
