param([string]$Python)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$venvPython = Join-Path $projectRoot '.tools\python\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    if (-not $Python) {
        $candidate = Get-Command python -ErrorAction SilentlyContinue
        if ($candidate) { $Python = $candidate.Source }
    }
    if (-not $Python) {
        $bundledPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
        if (Test-Path -LiteralPath $bundledPython) { $Python = $bundledPython }
    }
    if (-not $Python) { throw 'Install Python 3.12 x64, or pass -Python C:\path\to\python.exe.' }
    & $Python -m venv .tools\python
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the build Python environment.' }
}
& $venvPython -m pip install --disable-pip-version-check --cache-dir .tools\pip-cache -r scripts\requirements-build.txt
if ($LASTEXITCODE -ne 0) { throw 'Build dependency download failed.' }
& $venvPython scripts\fetch-qt.py
if ($LASTEXITCODE -ne 0) { throw 'Qt/MinGW download failed.' }
Write-Host 'Ready. Run: powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1'
