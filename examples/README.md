# Examples

`commands.ndjson` contains requests that can be pasted one line at a time into a 115200-baud serial terminal. The device responds with one JSON object per line and echoes the request `id`.

The Python client is preferred for automation:

```powershell
python tools/espbarcode.py --port COM7 hello
python tools/espbarcode.py --port COM7 generate qr "LAB-TEST-001"
python tools/espbarcode.py --port COM7 generate gs1-128 "0109501101530003{FNC1}10ABC"
python tools/espbarcode.py --port COM7 download current-matrix.json --pbm current-matrix.pbm
python tools/espbarcode.py --port COM7 upload current-matrix.json
```
