param([switch]$Prepare)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage=[IO.Path]::GetFullPath((Join-Path $workspace 'build/package'))
$release=[IO.Path]::GetFullPath((Join-Path $workspace 'release'))
function Assert-Within([string]$Path,[string]$Parent) {
    $absolute=[IO.Path]::GetFullPath($Path)
    $prefix=[IO.Path]::GetFullPath($Parent).TrimEnd('\')+'\'
    if(-not $absolute.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Refusing path outside target directory: $absolute"}
    if(Test-Path -LiteralPath $absolute){
        if((Get-Item -LiteralPath $absolute -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Refusing reparse point: $absolute"}
    }
}
Assert-Within $stage $workspace
Assert-Within $release $workspace
if($Prepare){
    if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    exit 0
}
if(-not (Test-Path -LiteralPath (Join-Path $stage 'WhatInput.exe'))){throw 'Staged WhatInput.exe is missing.'}

# Stop only copies belonging to this workspace. This is deployment, not a test.
$processes=@(Get-Process WhatInput,WhatInputStats -ErrorAction SilentlyContinue)
foreach($process in $processes){
    $exe=$process.Path
    if(-not $exe){throw 'Cannot determine the path of a running WhatInput process. Close WhatInput from its tray menu before deploying.'}
    if(-not $exe.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){continue}
    if($process.ProcessName -eq 'WhatInputStats'){
        if(-not $process.CloseMainWindow()){throw 'Please close the old statistics window before deploying.'}
    }else{
        Start-Process -FilePath $exe -ArgumentList '--exit' -WorkingDirectory ([IO.Path]::GetDirectoryName($exe)) -WindowStyle Hidden -Wait
    }
    if(-not $process.WaitForExit(15000)){throw 'WhatInput is still running. Close it from its tray menu before deploying.'}
}
New-Item -ItemType Directory -Path $release -Force | Out-Null
$stamp=Join-Path $release '.whatinput-schema-version'
$oldVersion=if(Test-Path -LiteralPath $stamp){(Get-Content -LiteralPath $stamp -Raw).Trim()}else{''}
if($oldVersion -ne '3'){
    $data=Join-Path $release 'data'
    Assert-Within $data $release
    foreach($name in @('whatinput.sqlite3','whatinput.sqlite3-wal','whatinput.sqlite3-shm','whatinput.sqlite3.pre-category-v1.bak')){
        $target=Join-Path $data $name
        Assert-Within $target $release
        if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
    }
    Write-Output 'Old database removed for schema version 3 (no migration).'
}

# All runtime artifacts are generated; keep application data/config/logs/exports.
$generatedDirs=@('platforms','styles','generic','iconengines','imageformats','networkinformation','position','qml','qmltooling','resources','tls','translations')
foreach($name in $generatedDirs){
    $target=Join-Path $release $name
    Assert-Within $target $release
    if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Recurse -Force}
}
foreach($file in Get-ChildItem -LiteralPath $release -File){
    if($file.Extension -eq '.dll' -or $file.Name -in @('WhatInput.exe','WhatInputStats.exe','QtWebEngineProcess.exe','vc_redist.x64.exe')){
        Assert-Within $file.FullName $release
        Remove-Item -LiteralPath $file.FullName -Force
    }
}
foreach($item in Get-ChildItem -LiteralPath $stage -Force){Copy-Item -LiteralPath $item.FullName -Destination $release -Recurse -Force}
Set-Content -LiteralPath $stamp -Value '3' -Encoding ASCII
Write-Output "Deployed single-process Widgets build to $release"
