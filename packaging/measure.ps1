param([int]$Seconds=60,[string]$Output="docs/resource-sample.json")
$ErrorActionPreference="Stop"
if(Get-Process WhatInputStats -ErrorAction SilentlyContinue){throw "Close WhatInputStats before measuring collector-only resources."}
$process=Get-Process WhatInput
$cpuStart=$process.TotalProcessorTime.TotalSeconds
$started=Get-Date
$samples=@()
for($i=0;$i -lt $Seconds;$i++){
    Start-Sleep -Seconds 1
    $process.Refresh()
    $samples+=[pscustomobject]@{workingSet=$process.WorkingSet64;privateBytes=$process.PrivateMemorySize64;handles=$process.HandleCount;threads=$process.Threads.Count}
}
$elapsed=((Get-Date)-$started).TotalSeconds
$result=[pscustomobject]@{
    seconds=$elapsed
    cpuOneCorePercent=100*($process.TotalProcessorTime.TotalSeconds-$cpuStart)/$elapsed
    cpuMachinePercent=100*($process.TotalProcessorTime.TotalSeconds-$cpuStart)/$elapsed/[Environment]::ProcessorCount
    logicalProcessors=[Environment]::ProcessorCount
    privateMiB=($samples.privateBytes|Measure-Object -Average).Average/1MB
    workingSetMiB=($samples.workingSet|Measure-Object -Average).Average/1MB
    privatePeakMiB=($samples.privateBytes|Measure-Object -Maximum).Maximum/1MB
    workingSetPeakMiB=($samples.workingSet|Measure-Object -Maximum).Maximum/1MB
    handles=$process.HandleCount
    threads=$process.Threads.Count
}
$result | ConvertTo-Json | Set-Content -Encoding UTF8 $Output
$result
