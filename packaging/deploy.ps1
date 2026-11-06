param([ValidateSet('dev','release')][string]$Mode='dev',[switch]$Prepare)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=(Get-Content -LiteralPath (Join-Path $workspace 'VERSION') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid VERSION; expected major.minor.patch.'}
$stage=[IO.Path]::GetFullPath((Join-Path $workspace "build/package-$Mode"))
$destination=if($Mode -eq 'dev'){Join-Path $workspace 'dev_release'}else{Join-Path $workspace "release/$version"}
function Assert-Within([string]$Path,[string]$Parent){
    $absolute=[IO.Path]::GetFullPath($Path)
    $root=[IO.Path]::GetFullPath($Parent).TrimEnd('\')
    if(-not $absolute.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Path outside intended directory: $absolute"}
    $cursor=$absolute
    while($cursor -and $cursor.Length -gt $root.Length){
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Refusing reparse point: $cursor"}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
}
Assert-Within $stage $workspace
Assert-Within $destination $workspace
if($Mode -eq 'release' -and (Test-Path -LiteralPath $destination)){throw "Release already exists; remove it personally or change VERSION: $destination"}
if($Prepare){
    if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    exit 0
}
if(-not (Test-Path -LiteralPath (Join-Path $stage 'WhatInput.exe'))){throw 'Staged executable missing.'}
if($Mode -eq 'dev'){
    $expected=Join-Path $destination 'WhatInput.exe'
    foreach($process in @(Get-Process WhatInput -ErrorAction SilentlyContinue)){
        $exe=$process.Path
        if(-not $exe){throw 'Cannot read running WhatInput path. Exit it from the tray before deployment.'}
        if(-not $exe.Equals($expected,[StringComparison]::OrdinalIgnoreCase)){continue}
        Start-Process -FilePath $exe -ArgumentList '--exit' -WorkingDirectory $destination -WindowStyle Hidden -Wait
        if(-not $process.WaitForExit(15000)){throw 'Developer copy is still running.'}
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    # Preserve all developer data, config, caches, logs, exports and temp files.
    foreach($name in @('platforms','styles','generic','iconengines','imageformats','networkinformation','position','qml','qmltooling','resources','tls','translations')){
        $target=Join-Path $destination $name;Assert-Within $target $destination
        if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Recurse -Force}
    }
    foreach($file in Get-ChildItem -LiteralPath $destination -File){
        if($file.Extension -eq '.dll' -or $file.Name -in @('WhatInput.exe','WhatInputStats.exe','QtWebEngineProcess.exe','vc_redist.x64.exe')){
            Assert-Within $file.FullName $destination;Remove-Item -LiteralPath $file.FullName -Force
        }
    }
}else{
    # Publish from isolated staging only; never copy dev_release or its personal data.
    New-Item -ItemType Directory -Path $destination | Out-Null
}
foreach($item in Get-ChildItem -LiteralPath $stage -Force){Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse -Force}
Write-Output "Deployed $Mode $version to $destination"
