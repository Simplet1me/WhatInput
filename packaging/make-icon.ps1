$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$assets=Join-Path $PSScriptRoot '../assets'
$source=[Drawing.Image]::FromFile((Join-Path $assets 'ICON.png'))
$sizes=@(16,24,32,48,64,128,256)
$frames=New-Object 'System.Collections.Generic.List[byte[]]'
try {
    foreach($size in $sizes){
        $bitmap=New-Object Drawing.Bitmap($size,$size)
        $graphics=[Drawing.Graphics]::FromImage($bitmap)
        $stream=New-Object IO.MemoryStream
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $scale=[Math]::Min($size/$source.Width,$size/$source.Height)
            $width=[single]($source.Width*$scale)
            $height=[single]($source.Height*$scale)
            $graphics.DrawImage($source,[single](($size-$width)/2),[single](($size-$height)/2),$width,$height)
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally {$stream.Dispose();$graphics.Dispose();$bitmap.Dispose()}
    }
    $output=[IO.File]::Create((Join-Path $assets 'WhatInput.ico'))
    $writer=New-Object IO.BinaryWriter($output)
    try {
        $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
        $offset=6+16*$sizes.Count
        for($i=0;$i -lt $sizes.Count;$i++){
            $dimension=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
            $writer.Write([byte]$dimension);$writer.Write([byte]$dimension)
            $writer.Write([byte]0);$writer.Write([byte]0)
            $writer.Write([uint16]1);$writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
            $offset+=$frames[$i].Length
        }
        foreach($frame in $frames){$writer.Write([byte[]]$frame)}
    } finally {$writer.Dispose()}
} finally {$source.Dispose()}
