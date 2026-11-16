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
function Remove-PristineBackup {
    $path=[IO.Path]::GetFullPath($backup)
    if ([IO.Path]::GetDirectoryName($path) -ne $parentDir.FullName -or $path -ne "$originalTarget.previous-$id") { return }
    $receipt=Join-Path $path 'licenses\windows-files.json'
    if (!(Test-Path -LiteralPath $receipt -PathType Leaf)) { return }
    $inventory=Get-Content -Raw -Encoding UTF8 -LiteralPath $receipt | ConvertFrom-Json
    $dirs=@{}
    foreach ($entry in $inventory.PSObject.Properties.Name) {
        while ($entry.Contains('/')) { $entry=$entry.Substring(0,$entry.LastIndexOf('/')); $dirs[$entry]=$true }
    }
    $pending=New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($path)
    while ($pending.Count) {
        $folder=$pending.Pop()
        if ((Get-Item -Force -LiteralPath $folder).Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
        foreach ($file in Get-ChildItem -Force -LiteralPath $folder) {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
            $name=$file.FullName.Substring($path.Length+1).Replace('\','/')
            if ($file.PSIsContainer) {
                if (!$dirs.ContainsKey($name)) { return }
                $pending.Push($file.FullName)
            } elseif ($name -notin @('Uninstall.exe','licenses/windows-files.json')) {
                $expected=$inventory.PSObject.Properties[$name]
                if (!$expected -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $expected.Value) { return }
            }
        }
    }
    Remove-Item -LiteralPath $path -Recurse -Force
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
    # Setup installs an uninstaller; the Portable ZIP never contains it. Update the
    # launched installation in place even when this shell cannot see its registration.
    $installed = Test-Path -LiteralPath (Join-Path $target 'Uninstall.exe') -PathType Leaf
    $registeredDir = (Get-ItemProperty -LiteralPath $installKey -ErrorAction SilentlyContinue).InstallDir
    $registeredHere = $registeredDir -and ([IO.Path]::GetFullPath($registeredDir).TrimEnd('\') -eq $target)
    $installed = $installed -or $registeredHere
    $newExe = Join-Path $stage 'NimboDesktop.exe'
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($newExe).FileVersion -ne $Version) {
        throw 'The executable version does not match the requested release.'
    }
    $expectedExeHash = (Get-FileHash -LiteralPath $newExe -Algorithm SHA256).Hash
    if ($installed) {
        $profilePath = [IO.Path]::GetFullPath($ProfileDir).TrimEnd('\')
        if ($profilePath.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or $profilePath -eq $target) {
            throw 'The profile is inside the installation. Update manually to keep it in place.'
        }
        $uninstaller = Join-Path $target 'Uninstall.exe'
        if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) { $uninstaller = Join-Path $stage 'uninstall-update.bin' }
        Copy-Item -LiteralPath $uninstaller -Destination (Join-Path $stage 'Uninstall.exe')
        [IO.Directory]::Move($target,$backup)
        $oldMoved = $true
    } else {
        # Keep the previous portable directory and any personal files in their original location.
        $target = Join-Path $parentDir.FullName "Nimbo-Desktop-$Version-portable"
        if (Test-Path -LiteralPath $target) {
            # The existing directory may contain personal files or a different build.
            $target = "$target-$id"
        }
    }
    [IO.Directory]::Move($stage,$target)
    $newInstalled = $true
    $client = Start-Client
    if ($client.WaitForExit(5000)) { throw 'The updated client exited during startup.' }
    $runningExe = $client.MainModule.FileName
    if ($runningExe -ne (Join-Path $target 'NimboDesktop.exe') -or
        (Get-FileHash -LiteralPath $runningExe -Algorithm SHA256).Hash -ne $expectedExeHash) {
        $null = $client.CloseMainWindow()
        if (!$client.WaitForExit(5000)) { throw 'The new client could not be verified or closed.' }
        throw 'The running executable does not match the downloaded release.'
    }
    if ($installed -and $registeredHere) {
        Set-ItemProperty -LiteralPath $uninstallKey -Name DisplayVersion -Value $Version -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $archive -ErrorAction SilentlyContinue
    if ($oldMoved) {
        # Cleanup failure must never roll back a running, verified new release.
        try { Remove-PristineBackup } catch { Write-Warning $_.Exception.Message }
    }
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
