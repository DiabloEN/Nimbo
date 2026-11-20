param(
    [Parameter(Mandatory)][string]$InstallDir,
    [Parameter(Mandatory)][string]$ProfileDir,
    [Parameter(Mandatory)][int]$ParentId,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$Digest,
    [string]$ReadyFile = '',
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
# Also support older clients that inherit PowerShell 7's module search path.
# Only built-in modules are needed by this installer.
$env:PSModulePath = [IO.Path]::Combine($PSHOME,'Modules')
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
$progressWindow = $null
function Set-UpdateProgress([int]$value,[string]$text) {
    if (!$ReadyFile) { return }
    if (!$script:progressWindow) {
        Add-Type -AssemblyName System.Windows.Forms,System.Drawing
        $script:progressWindow=New-Object Windows.Forms.Form
        $script:progressWindow.Text='Nimbo'
        $script:progressWindow.ClientSize=New-Object Drawing.Size(360,88)
        $script:progressWindow.StartPosition='CenterScreen'
        $script:progressWindow.FormBorderStyle='FixedDialog'
        $script:progressWindow.ControlBox=$false
        $script:progressWindow.BackColor=[Drawing.Color]::FromArgb(16,16,16)
        $script:progressLabel=New-Object Windows.Forms.Label
        $script:progressLabel.SetBounds(20,16,320,26)
        $script:progressLabel.ForeColor=[Drawing.Color]::FromArgb(237,237,237)
        $script:progressWindow.Controls.Add($script:progressLabel)
        $script:progressBar=New-Object Windows.Forms.ProgressBar
        $script:progressBar.SetBounds(20,52,320,6)
        $script:progressWindow.Controls.Add($script:progressBar)
        $script:progressWindow.Show()
    }
    $script:progressLabel.Text=$text
    $script:progressBar.Value=$value
    [Windows.Forms.Application]::DoEvents()
}
function Start-Client {
    $entry=Join-Path $target 'Nimbo.exe'
    if (!(Test-Path -LiteralPath $entry -PathType Leaf)) { $entry=Join-Path $target 'NimboDesktop.exe' }
    Start-Process -FilePath $entry -WorkingDirectory $target -ArgumentList ('--data-dir "' + $ProfileDir.TrimEnd('\') + '"') -PassThru
}
function Move-UpdateDirectory([string]$source,[string]$destination) {
    for($attempt=1;$attempt -le 20;$attempt++){
        try { [IO.Directory]::Move($source,$destination); return }
        catch {
            $cause=$_.Exception.GetBaseException()
            if ($attempt -eq 20 -or (Test-Path -LiteralPath $destination) -or
                ($cause -isnot [IO.IOException] -and $cause -isnot [UnauthorizedAccessException])){throw}
            Start-Sleep -Milliseconds 250
        }
    }
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
    $exe = Join-Path $target 'Nimbo.exe'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { $exe=Join-Path $target 'NimboDesktop.exe' }
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The existing installation was not found.' }
    $parent = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
    if ($parent) {
        if ($parent.Path -ne $exe) { throw 'The active client does not match this installation.' }
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Digest) { throw 'Release checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive,$stage)
    $legacyExe=Join-Path $stage 'NimboDesktop.exe'
    $newExe=Join-Path $stage 'Nimbo.exe'
    if ((Test-Path -LiteralPath $legacyExe -PathType Leaf) -and !(Test-Path -LiteralPath $newExe)) {
        Rename-Item -LiteralPath $legacyExe -NewName 'Nimbo.exe'
        # The immutable legacy ZIP contains inventories under the previous filename.
        $binaries=Join-Path $stage 'licenses\windows-binaries.sha256'
        $receipt=Join-Path $stage 'licenses\windows-files.json'
        $utf8=New-Object Text.UTF8Encoding($false)
        if(Test-Path -LiteralPath $binaries){
            [IO.File]::WriteAllText($binaries,([IO.File]::ReadAllText($binaries) -replace '(?m)  NimboDesktop\.exe(?=\r?$)','  Nimbo.exe'),$utf8)
        }
        if(Test-Path -LiteralPath $receipt){
            $inventory=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8 | ConvertFrom-Json
            $hash=$inventory.'NimboDesktop.exe'
            if($hash){
                $inventory.PSObject.Properties.Remove('NimboDesktop.exe')
                $inventory | Add-Member NoteProperty 'Nimbo.exe' $hash
                $inventory.'licenses/windows-binaries.sha256'=(Get-FileHash -LiteralPath $binaries).Hash.ToLowerInvariant()
                [IO.File]::WriteAllText($receipt,($inventory | ConvertTo-Json),$utf8)
            }
        }
    }
    foreach ($file in @('Nimbo.exe','Qt6Core.dll','update-desktop.ps1','assets\room-preview.png')) {
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
    $installTarget=$target
    $legacyPath=Join-Path $env:LOCALAPPDATA 'Programs\Nimbo Desktop Preview'
    if ($registeredHere -and $target -eq $legacyPath) {
        $installTarget=Join-Path $parentDir.FullName 'Nimbo'
        if(Test-Path -LiteralPath $installTarget){throw 'The Nimbo destination already exists. Use Setup to repair this installation.'}
    }
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($newExe).FileVersion -ne $Version) {
        throw 'The executable version does not match the requested release.'
    }
    $expectedExeHash = (Get-FileHash -LiteralPath $newExe -Algorithm SHA256).Hash
    # Validate and unpack before asking the live client to close. A failed helper
    # must not turn an otherwise usable client into a silent exit/relaunch loop.
    if ($ReadyFile) {
        if ([IO.Path]::GetFullPath($ReadyFile) -ne (Join-Path $PSScriptRoot 'ready')) { throw 'Invalid installer handshake path.' }
        [IO.File]::WriteAllText($ReadyFile,'ready')
    }
    if ($parent -and !$parent.WaitForExit(20000)) { throw 'Close Nimbo before updating.' }
    $canRestart = $true
    Set-UpdateProgress 35 $(if($env:NIMBO_UPDATE_LANGUAGE -eq 'en'){'Installing update…'}else{'Устанавливаем обновление…'})
    if ($installed) {
        $profilePath = [IO.Path]::GetFullPath($ProfileDir).TrimEnd('\')
        if ($profilePath.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or $profilePath -eq $target) {
            throw 'The profile is inside the installation. Update manually to keep it in place.'
        }
        $uninstaller = Join-Path $target 'Uninstall.exe'
        if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) { $uninstaller = Join-Path $stage 'uninstall-update.bin' }
        Copy-Item -LiteralPath $uninstaller -Destination (Join-Path $stage 'Uninstall.exe')
        Move-UpdateDirectory $target $backup
        $oldMoved = $true
        $target=$installTarget
    } else {
        # Keep the previous portable directory and any personal files in their original location.
        $target = Join-Path $parentDir.FullName "Nimbo-Desktop-$Version-portable"
        if (Test-Path -LiteralPath $target) {
            # The existing directory may contain personal files or a different build.
            $target = "$target-$id"
        }
    }
    Move-UpdateDirectory $stage $target
    $newInstalled = $true
    if($registeredHere -and $target -ne $originalTarget){
        Set-ItemProperty -LiteralPath $installKey -Name InstallDir -Value $target
    }
    Set-UpdateProgress 75 $(if($env:NIMBO_UPDATE_LANGUAGE -eq 'en'){'Starting Nimbo…'}else{'Запускаем Nimbo…'})
    $client = Start-Client
    if ($client.WaitForExit(5000)) { throw 'The updated client exited during startup.' }
    $runningExe = $client.MainModule.FileName
    if ($runningExe -ne (Join-Path $target 'Nimbo.exe') -or
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
    Set-UpdateProgress 100 $(if($env:NIMBO_UPDATE_LANGUAGE -eq 'en'){'Update installed'}else{'Обновление установлено'})
} catch {
    $reason = $_.Exception.Message
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'failed'),$reason)
    if ($newInstalled) { Move-UpdateDirectory $target $stage }
    if ($oldMoved) { Move-UpdateDirectory $backup $originalTarget }
    $target = $originalTarget
    if($registeredHere -and $oldMoved){Set-ItemProperty -LiteralPath $installKey -Name InstallDir -Value $originalTarget}
    if ($canRestart) { Start-Client | Out-Null }
    if ($NonInteractive) {throw $reason}
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($reason,'Nimbo. — обновление') | Out-Null
    exit 1
} finally {
    if ($progressWindow) { $progressWindow.Close();$progressWindow.Dispose() }
}
