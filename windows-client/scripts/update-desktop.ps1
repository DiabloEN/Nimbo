param(
    [Parameter(Mandatory)][string]$InstallDir,
    [Parameter(Mandatory)][string]$ProfileDir,
    [Parameter(Mandatory)][int]$ParentId,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$Digest
)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$parentDir = [IO.Directory]::GetParent($target)
$archive = Join-Path $PSScriptRoot 'client.zip'
$id = [guid]::NewGuid().ToString('N')
$stage = Join-Path $parentDir.FullName ".nimbo-stage-$id"
$backup = "$target.previous-$id"
$oldMoved = $false
$newInstalled = $false
$originalTarget = $target
$canRestart = $false
function Start-Client {
    Start-Process -FilePath (Join-Path $target 'NimboDesktop.exe') -WorkingDirectory $target -ArgumentList ('--data-dir "' + $ProfileDir.TrimEnd('\') + '"') -PassThru
}
try {
    $exe = Join-Path $target 'NimboDesktop.exe'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The existing installation was not found.' }
    $parent = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
    if ($parent) {
        if ($parent.Path -ne $exe) { throw 'The active client does not match this installation.' }
        if (!$parent.WaitForExit(20000)) { throw 'Close Nimbo before updating.' }
    }
    $canRestart = $true
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Digest) { throw 'Release checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive,$stage)
    foreach ($file in @('NimboDesktop.exe','Qt6Core.dll','update-desktop.ps1','assets\room-preview.png')) {
        if (!(Test-Path -LiteralPath (Join-Path $stage $file) -PathType Leaf)) { throw 'Incomplete Windows release.' }
    }
    $installKey = 'HKCU:\Software\Nimbo\DesktopPreview'
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NimboDesktopPreview'
    $installed = (Get-ItemProperty -LiteralPath $installKey -ErrorAction SilentlyContinue).InstallDir -eq $target -and
                 (Test-Path -LiteralPath (Join-Path $target 'Uninstall.exe') -PathType Leaf)
    if ($installed) {
        $profilePath = [IO.Path]::GetFullPath($ProfileDir).TrimEnd('\')
        if ($profilePath.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or $profilePath -eq $target) {
            throw 'The profile is inside the installation. Update manually to keep it in place.'
        }
        Copy-Item -LiteralPath (Join-Path $target 'Uninstall.exe') -Destination $stage
        [IO.Directory]::Move($target,$backup)
        $oldMoved = $true
    } else {
        # Keep the previous portable directory and any personal files in their original location.
        $target = Join-Path $parentDir.FullName "Nimbo-Desktop-$Version-portable"
        if (Test-Path -LiteralPath $target) { throw 'The destination version already exists. Open it or move it before updating.' }
    }
    [IO.Directory]::Move($stage,$target)
    $newInstalled = $true
    $client = Start-Client
    if ($client.WaitForExit(5000)) { throw 'The updated client exited during startup.' }
    if ($installed) {
        Set-ItemProperty -LiteralPath $uninstallKey -Name DisplayVersion -Value $Version -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $archive -ErrorAction SilentlyContinue
} catch {
    $reason = $_.Exception.Message
    if ($newInstalled) { [IO.Directory]::Move($target,$stage) }
    if ($oldMoved) { [IO.Directory]::Move($backup,$target) }
    $target = $originalTarget
    if ($canRestart) { Start-Client | Out-Null }
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($reason,'Nimbo. — обновление') | Out-Null
    exit 1
}
