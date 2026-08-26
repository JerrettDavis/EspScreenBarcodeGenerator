param(
    [Parameter(Mandatory = $false)]
    [switch]$EnableSanitizers,

    [Parameter(Mandatory = $false)]
    [string]$BuildDirectory = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $Root ".build/native-validation"
}

Push-Location $Root
try {
    python -c "import pymupdf" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "PyMuPDF is required. Run: python -m pip install -r tools/validation-requirements.txt"
    }

    $Sanitizers = if ($EnableSanitizers) { "ON" } else { "OFF" }
    $Generator = @()
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = @("-G", "Ninja")
    }

    $SanitizerArgument = "-DESPBARCODE_ENABLE_SANITIZERS=$Sanitizers"
    & cmake -S $Root -B $BuildDirectory @Generator `
        -DCMAKE_BUILD_TYPE=Debug `
        $SanitizerArgument
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

    & cmake --build $BuildDirectory --parallel
    if ($LASTEXITCODE -ne 0) { throw "Native build failed" }

    & ctest --test-dir $BuildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Native C++ tests failed" }

    & python -m unittest discover -s (Join-Path $Root "tests") -p "test_*.py" -v
    if ($LASTEXITCODE -ne 0) { throw "Python tests failed" }

    & python (Join-Path $Root "tests/static_firmware_checks.py")
    if ($LASTEXITCODE -ne 0) { throw "Static firmware checks failed" }

    $CliCandidates = @(
        (Join-Path $BuildDirectory "barcode_native.exe"),
        (Join-Path $BuildDirectory "Debug/barcode_native.exe"),
        (Join-Path $BuildDirectory "barcode_native")
    )
    $Cli = $CliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($Cli)) {
        throw "Could not locate the native barcode validation executable"
    }

    & python (Join-Path $Root "tests/validate_symbols.py") `
        --cli $Cli `
        --report (Join-Path $Root "docs/validation/independent-decoder-report.json")
    if ($LASTEXITCODE -ne 0) { throw "Independent decoder validation failed" }

    Write-Host "All portable validation suites passed."
}
finally {
    Pop-Location
}
