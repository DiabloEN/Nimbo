$ErrorActionPreference = 'Stop'
$toolsDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.tools/openh264-2.6.0'
New-Item -ItemType Directory -Force $toolsDir | Out-Null
$packages = @(
    @{ File='source.zip'; Url='https://codeload.github.com/cisco/openh264/zip/refs/tags/v2.6.0'; Hash='B1CA643EADBBF16F74C86A66AB487F38691D8DBDDA435683B149B2CE2C8077EC' },
    @{ File='nasm.zip'; Url='https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip'; Hash='3EE4782247BCB874378D02F7EAB4E294A84D3D15F3F6EE2DE2F47A46AA7226E6' }
)
foreach ($package in $packages) {
    $archive = Join-Path $toolsDir $package.File
    if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash -ne $package.Hash) {
        Invoke-WebRequest -Uri $package.Url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $package.Hash) { throw "Checksum mismatch for $archive" }
    & tar.exe -xf $archive -C $toolsDir
    if ($LASTEXITCODE) { throw "Could not extract $archive" }
}
