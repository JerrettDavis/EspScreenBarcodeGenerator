export function isSupported() { return 'BarcodeDetector' in window; }
export async function decode(input) {
  if (!input.files?.length) throw new Error('Choose an image first.');
  if (!('BarcodeDetector' in window)) throw new Error('Barcode image detection is unavailable in this browser. Try Chrome on Android.');
  const bitmap = await createImageBitmap(input.files[0]);
  try {
    const results = await new BarcodeDetector().detect(bitmap);
    if (!results.length) throw new Error('No barcode was found in that image.');
    return { data: results[0].rawValue, format: results[0].format };
  } finally { bitmap.close(); }
}
