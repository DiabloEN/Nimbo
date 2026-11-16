param([Parameter(Mandatory)][string]$OldPortable,[Parameter(Mandatory)][string]$Archive)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$source=(Resolve-Path -LiteralPath $OldPortable).Path
$zip=(Resolve-Path -LiteralPath $Archive).Path
$manifest=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $zip) 'windows-x64.json') -Raw | ConvertFrom-Json
$base=(Resolve-Path -LiteralPath (Join-Path $root 'artifacts/native-desktop')).Path
$work=Join-Path $base ('update-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    foreach($mode in @('portable','installed')){
        $run=New-Item -ItemType Directory -Path (Join-Path $work $mode)
        $old=Join-Path $run.FullName 'previous-client'
        Copy-Item -LiteralPath $source -Destination $old -Recurse
        $profile=New-Item -ItemType Directory -Path (Join-Path $run.FullName 'profile')
        $profileState=Join-Path $profile.FullName 'profile.dpapi'
        Add-Type -AssemblyName System.Security
        $fixture='{"format":"nimbo-profile","version":1,"name":"QA Update","avatar":"","servers":[],"hotkeys":{}}'
        [IO.File]::WriteAllBytes($profileState,[Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($fixture),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
        Copy-Item -LiteralPath $zip -Destination (Join-Path $run.FullName 'client.zip')
        $helper=Join-Path $run.FullName 'update-desktop.ps1'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'update-desktop.ps1') -Destination $helper
        $destination=Join-Path $run.FullName ("Nimbo-Desktop-"+$manifest.version+"-portable")
        $registration=@{}
        if($mode -eq 'installed'){
            $destination=$old
            [IO.File]::WriteAllText((Join-Path $old 'Uninstall.exe'),'QA uninstaller marker')
        }
        # Exercise both installation layouts without editing the user's Windows registration.
        function Get-ItemProperty {param($LiteralPath,$ErrorAction)
            if($LiteralPath -ne 'HKCU:\Software\Nimbo\DesktopPreview'){throw 'Unexpected registry read'}
            if($mode -eq 'installed'){@{InstallDir=$old}}
        }
        function Set-ItemProperty {param($LiteralPath,$Name,$Value,$ErrorAction)
            if($Name -ne 'DisplayVersion' -or !$LiteralPath.EndsWith('\Uninstall\NimboDesktopPreview')){throw 'Unexpected registry write'}
            $registration.DisplayVersion=$Value
        }
        $previous=Start-Process -FilePath (Join-Path $old 'NimboDesktop.exe') -ArgumentList @('--data-dir',('"'+$profile.FullName+'"'),'--screenshot',('"'+(Join-Path $run.FullName 'old.png')+'"')) -WindowStyle Hidden -PassThru
        try {
            if(!$previous.WaitForExit(15000) -or $previous.ExitCode -ne 0){throw 'Old client did not start and close cleanly'}
            $before=(Get-FileHash -LiteralPath $profileState).Hash
            $library=Join-Path $profile.FullName 'profiles.dpapi'
            $libraryBefore=(Get-FileHash -LiteralPath $library).Hash
            & $helper -InstallDir $old -ProfileDir $profile.FullName -ParentId $previous.Id -Version $manifest.version -Digest $manifest.sha256
            $expected=(Join-Path $destination 'NimboDesktop.exe')
            $running=@(Get-Process -Name NimboDesktop -ErrorAction SilentlyContinue | Where-Object Path -eq $expected)
            if($running.Count -ne 1){throw "Expected exactly one updated client in $destination"}
            if((Get-FileHash -LiteralPath $expected).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'build-media/NimboDesktop.exe')).Hash){throw 'Updated executable differs from build'}
            if((Get-FileHash -LiteralPath $profileState).Hash -ne $before){throw 'Profile changed during update'}
            if((Get-FileHash -LiteralPath $library).Hash -ne $libraryBefore){throw 'Profile library changed during update'}
            if($mode -eq 'installed' -and ($registration.DisplayVersion -ne $manifest.version -or !(Test-Path -LiteralPath (Join-Path $destination 'Uninstall.exe')))){throw 'Installed registration or uninstaller missing'}
            Write-Output "$mode verified: new executable running, profile preserved"
        } finally {
            if(!$previous.HasExited){$previous.Kill();$previous.WaitForExit()}
            $previous.Dispose()
            foreach($process in @(Get-Process -Name NimboDesktop -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($run.FullName+'\',[StringComparison]::OrdinalIgnoreCase) })){
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
