$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$setup = Join-Path $root '.tools\media\msvc\setup_x64.bat'
if (Test-Path -LiteralPath $setup) {
    $command = '""' + $setup + '" && set"'
} else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools 2022 with Desktop development with C++ and Windows SDK.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'The MSVC x64 build tools are missing.' }
    $setup = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    $command = '""' + $setup + '" -arch=x64 -host_arch=x64 && set"'
}
$environment = & $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
$seen = @{}
foreach ($line in $environment) {
    if ($line -match '^(PATH|INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCToolsVersion|WindowsSdkDir|WindowsSdkBinPath|WindowsSDKVersion|VSCMD_ARG_HOST_ARCH|VSCMD_ARG_TGT_ARCH)=(.*)$') {
        $key = $matches[1]
        if (-not $seen.ContainsKey($key)) {
            [Environment]::SetEnvironmentVariable($key, $matches[2], 'Process')
            $seen[$key] = $true
        }
    }
}
