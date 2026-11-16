$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$python = Join-Path $root '.tools\python\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { & "$PSScriptRoot\bootstrap-windows.ps1" }
& $python "$PSScriptRoot\fetch-qt.py" --msvc
if ($LASTEXITCODE -ne 0) { throw 'Qt MSVC setup failed.' }
$downloads = Join-Path $root '.tools\media'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$packages = @(
    @{ Name='livekit-sdk-windows-x64-1.12.3.zip'; Url='https://github.com/livekit/client-sdk-cpp/releases/download/v1.12.3/livekit-sdk-windows-x64-1.12.3.zip'; Hash='81c39cb1469d873a657a1931ad40ead7cfc42d337d61b56de57a16beb95f8a70'; Folder='sdk' },
    @{ Name='livekit_1.13.10_windows_amd64.zip'; Url='https://github.com/livekit/livekit/releases/download/v1.13.10/livekit_1.13.10_windows_amd64.zip'; Hash='fd4b7c2c3c1e4c71adaa4f6a046ad1c0abaaaf1e09fae904b35bf231ffae11d2'; Folder='server' }
)
foreach ($package in $packages) {
    $archive = Join-Path $downloads $package.Name
    if (-not (Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) {
        Invoke-WebRequest -Uri $package.Url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) { throw 'LiveKit checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $downloads $package.Folder) -Force
}
& "$PSScriptRoot\bootstrap-openh264.ps1"
& "$PSScriptRoot\enter-msvc.ps1"
Write-Host 'Media dependencies ready. Run scripts\build-media.ps1.'
