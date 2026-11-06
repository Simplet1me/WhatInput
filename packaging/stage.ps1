param(
    [Parameter(Mandatory=$true)][ValidateSet('dev','release')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$RedistRoot
)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage=Join-Path $workspace "build/package-$Mode"
$binary=Join-Path $workspace "build/$Mode/WhatInput.exe"
Copy-Item -LiteralPath $binary -Destination $stage
Copy-Item -LiteralPath (Join-Path $workspace 'THIRD_PARTY.md') -Destination $stage
if($Mode -eq 'dev'){
    Copy-Item -LiteralPath (Join-Path $workspace 'README.md') -Destination $stage
    & (Join-Path $QtRoot 'bin/windeployqt.exe') --release --no-translations --dir $stage (Join-Path $stage 'WhatInput.exe')
    if($LASTEXITCODE -ne 0){throw 'Qt deployment failed.'}
}else{
    # Only the Widgets platform/style and formats actually used by this application.
    $plugins=@('platforms/qwindows.dll','styles/qmodernwindowsstyle.dll','imageformats/qico.dll','imageformats/qsvg.dll','iconengines/qsvgicon.dll')
    $inputs=@((Join-Path $stage 'WhatInput.exe'))
    foreach($plugin in $plugins){
        $source=Join-Path $QtRoot "plugins/$plugin"
        if(-not (Test-Path -LiteralPath $source)){throw "Required plugin missing: $source"}
        $target=Join-Path $stage $plugin
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        $inputs+=$target
    }
    & (Join-Path $QtRoot 'bin/windeployqt.exe') --release --no-plugins --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-ffmpeg --dir $stage @inputs
    if($LASTEXITCODE -ne 0){throw 'Minimal Qt dependency deployment failed.'}
    $crt=Join-Path $RedistRoot 'x64/Microsoft.VC143.CRT'
    if(-not (Test-Path -LiteralPath $crt)){
        $candidates=@(Get-ChildItem -LiteralPath (Join-Path $RedistRoot 'x64') -Directory | Where-Object {$_.Name -match '^Microsoft\.VC\d+\.CRT$'})
        if($candidates.Count -ne 1){throw 'Cannot locate the x64 MSVC runtime directory.'}
        $crt=$candidates[0].FullName
    }
    foreach($dll in Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File){Copy-Item -LiteralPath $dll.FullName -Destination $stage}
}
