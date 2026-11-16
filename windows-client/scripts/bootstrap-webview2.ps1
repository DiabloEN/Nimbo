$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$version='1.0.3650.58'
$dir=Join-Path $root ".tools/webview2-$version"
if(Test-Path -LiteralPath "$dir/build/native/x64/WebView2LoaderStatic.lib"){return}
$archive="$dir.zip"
Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile $archive
if((Get-FileHash $archive).Hash -ne '911A472128C82AC8BAA0C486C23342CC9DD6E7DC50D754E676726642CA065C60'){throw 'Unexpected WebView2 SDK checksum'}
Expand-Archive -LiteralPath $archive -DestinationPath $dir -Force
