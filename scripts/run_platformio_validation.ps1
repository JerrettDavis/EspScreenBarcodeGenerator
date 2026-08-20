$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $Root
try {
    python -m platformio --version | Out-Host
    python -m platformio test -e native
    python -m platformio run -e esp32dev
}
finally {
    Pop-Location
}
