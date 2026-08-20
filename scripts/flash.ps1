param(
    [Parameter(Mandatory = $false)]
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $Root
try {
    python -m platformio run -e esp32dev
    if ([string]::IsNullOrWhiteSpace($Port)) {
        python -m platformio run -e esp32dev -t upload
    }
    else {
        python -m platformio run -e esp32dev -t upload --upload-port $Port
    }
}
finally {
    Pop-Location
}
