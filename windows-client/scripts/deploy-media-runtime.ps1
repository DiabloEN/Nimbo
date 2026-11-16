param([Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$qt = Join-Path $root '.tools\qt-msvc'
$sdk = Join-Path $root '.tools\media\sdk\livekit-sdk-windows-x64-1.12.3'
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'Nimbo.exe'))) { throw 'Nimbo.exe is missing from the deployment directory.' }
& "$qt\bin\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --dir $Destination (Join-Path $Destination 'Nimbo.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
foreach ($dll in @('livekit.dll', 'livekit_ffi.dll')) { Copy-Item -LiteralPath (Join-Path $sdk "bin\$dll") -Destination $Destination -Force }
$redistBase = Join-Path $root '.tools\media\msvc\VC\Redist\MSVC'
if (-not (Test-Path -LiteralPath $redistBase)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $redistBase = Join-Path $installation 'VC\Redist\MSVC'
}
$crt = Get-ChildItem -LiteralPath $redistBase -Filter 'Microsoft.VC143.CRT' -Directory -Recurse |
    Where-Object { $_.Parent.Name -eq 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'MSVC x64 redistributable DLLs are missing.' }
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | Copy-Item -Destination $Destination -Force
$d3d = Join-Path $root '.tools\media\msvc\Windows Kits\10\Redist\D3D\x64'
if (-not (Test-Path -LiteralPath $d3d)) { $d3d = "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\D3D\x64" }
foreach ($dll in @('dxcompiler.dll', 'dxil.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $d3d $dll))) { throw "Windows SDK redistributable is missing $dll." }
    Copy-Item -LiteralPath (Join-Path $d3d $dll) -Destination $Destination -Force
}
foreach ($dll in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Multimedia.dll', 'Qt6Widgets.dll', 'livekit.dll', 'livekit_ffi.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Destination $dll))) { throw "Missing runtime: $dll" }
}
# QtTest is used only in build-media, never copied into the final client package.
if (Test-Path -LiteralPath (Join-Path $Destination 'nimbo_tests.exe')) {
    Copy-Item -LiteralPath "$qt\bin\Qt6Test.dll" -Destination $Destination -Force
    Copy-Item -LiteralPath "$qt\plugins\platforms\qoffscreen.dll" -Destination (Join-Path $Destination 'platforms') -Force
}

Copy-Item -LiteralPath (Join-Path $root ".tools/media/mediapipe/libmediapipe.dll") -Destination $Destination -Force
