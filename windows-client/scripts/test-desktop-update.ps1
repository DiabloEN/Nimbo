param([Parameter(Mandatory)][string]$OldPortable,[Parameter(Mandatory)][string]$Archive,[switch]$UsePreviousHelper,[string[]]$Modes=@('portable','occupied-portable','installed','installed-no-registry','registered-no-uninstaller','legacy-installed'))
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$source=(Resolve-Path -LiteralPath $OldPortable).Path
$zip=(Resolve-Path -LiteralPath $Archive).Path
$manifest=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $zip) 'windows-x64.json') -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath $zip).Hash -ne $manifest.sha256){throw 'Archive differs from update manifest'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$package=[IO.Compression.ZipFile]::OpenRead($zip)
try {
    $payload=$package.GetEntry('NimboDesktop.exe')
    if(!$payload){$payload=$package.GetEntry('Nimbo.exe')}
    if(!$payload){throw 'Missing client executable in update archive'}
    $stream=$payload.Open()
    try {$expectedHash=(Get-FileHash -InputStream $stream).Hash} finally {$stream.Dispose()}
} finally {$package.Dispose()}
$base=(Resolve-Path -LiteralPath (Join-Path $root 'artifacts/native-desktop')).Path
$work=Join-Path $base ('update-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    foreach($mode in $Modes){
        $run=New-Item -ItemType Directory -Path (Join-Path $work $mode)
        $old=Join-Path $run.FullName 'previous-client'
        if($mode -eq 'legacy-installed'){
            $null=New-Item -ItemType Directory -Path (Join-Path $run.FullName 'Programs')
            $old=Join-Path $run.FullName 'Programs\Nimbo Desktop Preview'
        }
        Copy-Item -LiteralPath $source -Destination $old -Recurse
        $profile=New-Item -ItemType Directory -Path (Join-Path $run.FullName 'profile')
        $profileState=Join-Path $profile.FullName 'profile.dpapi'
        Add-Type -AssemblyName System.Security
        $fixture='{"format":"nimbo-profile","version":1,"name":"QA Update","avatar":"","servers":[],"hotkeys":{}}'
        [IO.File]::WriteAllBytes($profileState,[Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($fixture),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
        Copy-Item -LiteralPath $zip -Destination (Join-Path $run.FullName 'client.zip')
        $helper=Join-Path $run.FullName 'update-desktop.ps1'
        $helperSource=if($UsePreviousHelper){$old}else{$PSScriptRoot}
        Copy-Item -LiteralPath (Join-Path $helperSource 'update-desktop.ps1') -Destination $helper
        $destination=Join-Path $run.FullName ("Nimbo-Desktop-"+$manifest.version+"-portable")
        $registration=@{}
        $occupied = $destination
        if($mode -in @('occupied-portable','installed-no-registry')){
            $null=New-Item -ItemType Directory -Path $occupied
            [IO.File]::WriteAllText((Join-Path $occupied 'keep.txt'),'personal')
        }
        if($mode -in @('installed','installed-no-registry','registered-no-uninstaller','legacy-installed')){
            $destination=$old
            if($mode -ne 'registered-no-uninstaller'){[IO.File]::WriteAllText((Join-Path $old 'Uninstall.exe'),'QA uninstaller marker')}
            $receipt=@{}
            foreach($file in Get-ChildItem -Recurse -File -LiteralPath $old){
                $receipt[$file.FullName.Substring($old.Length+1).Replace('\','/')]=(Get-FileHash -LiteralPath $file.FullName).Hash
            }
            $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $old 'licenses/windows-files.json') -Encoding UTF8
            if($mode -eq 'installed-no-registry'){[IO.File]::WriteAllText((Join-Path $old 'personal.txt'),'keep')}
            if($mode -eq 'legacy-installed'){$destination=Join-Path $run.FullName 'Programs\Nimbo'}
        }
        # Exercise both installation layouts without editing the user's Windows registration.
        function Get-ItemProperty {param($LiteralPath,$ErrorAction)
            if($LiteralPath -ne 'HKCU:\Software\Nimbo\DesktopPreview'){throw 'Unexpected registry read'}
            if($mode -in @('installed','registered-no-uninstaller','legacy-installed')){@{InstallDir=$old}}
        }
        function Set-ItemProperty {param($LiteralPath,$Name,$Value,$ErrorAction)
            if($Name -eq 'InstallDir' -and $LiteralPath -eq 'HKCU:\Software\Nimbo\DesktopPreview'){$registration.InstallDir=$Value;return}
            if($Name -ne 'DisplayVersion' -or !$LiteralPath.EndsWith('\Uninstall\NimboDesktopPreview')){throw 'Unexpected registry write'}
            $registration.DisplayVersion=$Value
        }
        $oldExe=Join-Path $old 'Nimbo.exe'
        if(!(Test-Path -LiteralPath $oldExe)){ $oldExe=Join-Path $old 'NimboDesktop.exe' }
        $previous=Start-Process -FilePath $oldExe -ArgumentList @('--data-dir',('"'+$profile.FullName+'"'),'--screenshot',('"'+(Join-Path $run.FullName 'old.png')+'"')) -WindowStyle Hidden -PassThru
        try {
            if(!$previous.WaitForExit(15000) -or $previous.ExitCode -ne 0){throw 'Old client did not start and close cleanly'}
            $before=(Get-FileHash -LiteralPath $profileState).Hash
            $library=Join-Path $profile.FullName 'profiles.dpapi'
            $libraryBefore=(Get-FileHash -LiteralPath $library).Hash
            $originalLocal=$env:LOCALAPPDATA
            try{
                if($mode -eq 'legacy-installed'){$env:LOCALAPPDATA=$run.FullName}
                $testOptions=@{}
                if(!$UsePreviousHelper){$testOptions.NonInteractive=$true}
                & $helper -InstallDir $old -ProfileDir $profile.FullName -ParentId $previous.Id -Version $manifest.version -Digest $manifest.sha256 @testOptions
            }finally{$env:LOCALAPPDATA=$originalLocal}
            if($mode -eq 'legacy-installed' -and $registration.InstallDir -ne $destination){throw 'Legacy install registration not migrated'}
            if($mode -in @('occupied-portable','installed-no-registry')){
                if([IO.File]::ReadAllText((Join-Path $occupied 'keep.txt')) -ne 'personal'){throw 'Occupied directory changed'}
            }
            if($mode -eq 'occupied-portable'){
                $alternate=@(Get-ChildItem -LiteralPath $run.FullName -Directory | Where-Object Name -like "Nimbo-Desktop-*-portable-*")
                if($alternate.Count -ne 1){throw 'Expected exactly one fresh portable directory'}
                $destination=$alternate[0].FullName
            }
            $entry=if($UsePreviousHelper -and (Split-Path $oldExe -Leaf) -eq 'NimboDesktop.exe'){'NimboDesktop.exe'}else{'Nimbo.exe'}
            $expected=(Join-Path $destination $entry)
            $running=@(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($entry)) -ErrorAction SilentlyContinue | Where-Object Path -eq $expected)
            if($running.Count -ne 1){throw "Expected exactly one updated client in $destination"}
            if((Get-FileHash -LiteralPath $expected).Hash -ne $expectedHash){throw 'Updated executable differs from the release archive'}
            if((Get-FileHash -LiteralPath $profileState).Hash -ne $before){throw 'Profile changed during update'}
            if((Get-FileHash -LiteralPath $library).Hash -ne $libraryBefore){throw 'Profile library changed during update'}
            if($mode -eq 'installed' -and ($registration.DisplayVersion -ne $manifest.version -or !(Test-Path -LiteralPath (Join-Path $destination 'Uninstall.exe')))){throw 'Installed registration or uninstaller missing'}
            $backups=@(Get-ChildItem -Directory -LiteralPath (Split-Path $old -Parent) -Filter ((Split-Path $old -Leaf)+'.previous-*'))
            if($mode -eq 'installed-no-registry'){
                if($backups.Count -ne 1 -or [IO.File]::ReadAllText((Join-Path $backups[0].FullName 'personal.txt')) -ne 'keep'){throw 'Custom files were lost'}
            }elseif($backups.Count){throw 'Verified temporary backup remained'}
            Write-Output "$mode verified: new executable running, profile preserved"
        } finally {
            if(!$previous.HasExited){$previous.Kill();$previous.WaitForExit()}
            $previous.Dispose()
            foreach($process in @(Get-Process -Name NimboDesktop,Nimbo -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($run.FullName+'\',[StringComparison]::OrdinalIgnoreCase) })){
                $null=$process.CloseMainWindow()
                if(!$process.WaitForExit(5000)){$process.Kill();$process.WaitForExit()}
                $process.Dispose()
            }
        }
    }
} finally {
    $resolved=(Resolve-Path -LiteralPath $work).Path
    if([IO.Path]::GetDirectoryName($resolved) -ne $base -or [IO.Path]::GetFileName($resolved) -notmatch '^update-test-[a-f0-9]{32}$'){throw 'Unexpected temporary directory'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
