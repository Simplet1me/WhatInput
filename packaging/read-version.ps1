$ErrorActionPreference='Stop'
$document=Join-Path $PSScriptRoot '../release.md'
$content=Get-Content -LiteralPath $document -Raw -Encoding utf8
$headings=[regex]::Matches($content,'(?m)^# (\d+\.\d+\.\d+)\s*$')
if($headings.Count -ne 1){throw 'release.md must contain exactly one version heading: # major.minor.patch'}
$headings[0].Groups[1].Value
