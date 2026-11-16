param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$saved = @{}
foreach ($item in Get-ChildItem Env:) { $saved[$item.Name] = $item.Value }
try {
    & "$PSScriptRoot\bootstrap-webview2.ps1"
    & "$PSScriptRoot\enter-msvc.ps1"
    $qt = Join-Path $root '.tools\qt-msvc'
    $sdk = Join-Path $root '.tools\media\sdk\livekit-sdk-windows-x64-1.12.3'
    $cmake = Join-Path $root '.tools\python\Lib\site-packages\cmake\data\bin'
    $env:PATH = "$qt\bin;$sdk\bin;$root\.tools\python\Scripts;$env:PATH"
    # The pip CMake distribution does not resolve its cmcldeps helper under Ninja.
    # version.rc has no project includes; its generated input is tracked by CMake.
    & "$cmake\cmake.exe" -S . -B build-media -G Ninja "-DCMAKE_PREFIX_PATH=$qt" '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=cl.exe' '-DCMAKE_NINJA_CMCLDEPS_RC=OFF' '-DNIMBO_MEDIA=ON' "-DNIMBO_LIVEKIT_ROOT=$sdk" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'Media configuration failed.' }
    & "$cmake\cmake.exe" --build build-media --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'Media build failed.' }
    & "$PSScriptRoot\deploy-media-runtime.ps1" -Destination (Join-Path $root 'build-media')
    if (-not $SkipTests) {
        & "$cmake\ctest.exe" --test-dir build-media --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Media tests failed.' }
    }
} finally {
    foreach ($item in @(Get-ChildItem Env:)) { if (-not $saved.ContainsKey($item.Name)) { [Environment]::SetEnvironmentVariable($item.Name, $null, 'Process') } }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
