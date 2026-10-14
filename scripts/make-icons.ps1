$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$assets = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets'
New-Item -ItemType Directory -Path $assets -Force | Out-Null

function New-GuardBitmap([int]$size, [string]$state) {
    $large = [Drawing.Bitmap]::new($size * 4, $size * 4)
    $g = [Drawing.Graphics]::FromImage($large)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.ScaleTransform($size / 16.0, $size / 16.0)
    $shield = [Drawing.Drawing2D.GraphicsPath]::new()
    $shield.StartFigure()
    $shield.AddBezier(32, 5, 24, 10, 17, 11, 9, 12)
    $shield.AddLine(9, 12, 9, 29)
    $shield.AddBezier(9, 29, 9, 43, 18, 53, 32, 59)
    $shield.AddBezier(32, 59, 46, 53, 55, 43, 55, 29)
    $shield.AddLine(55, 29, 55, 12)
    $shield.AddBezier(55, 12, 47, 11, 40, 10, 32, 5)
    $shield.CloseFigure()
    $colors = switch ($state) {
        'active' { @('#2DD4BF', '#059669', '#075E54') }
        'released' { @('#94A3B8', '#64748B', '#334155') }
        'error' { @('#FBBF24', '#F59E0B', '#92400E') }
    }
    $brush = [Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.Point]::new(0,5),[Drawing.Point]::new(0,59),[Drawing.ColorTranslator]::FromHtml($colors[0]),[Drawing.ColorTranslator]::FromHtml($colors[1]))
    $edge = [Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml($colors[2]),1.6)
    $g.FillPath($brush,$shield)
    $g.DrawPath($edge,$shield)
    $mark = [Drawing.SolidBrush]::new([Drawing.Color]::White)
    if ($state -eq 'active') {
        foreach($x in @(22,35)) {
            $bar = [Drawing.Drawing2D.GraphicsPath]::new()
            $bar.AddArc($x,20,7,7,180,180)
            $bar.AddArc($x,38,7,7,0,180)
            $bar.CloseFigure()
            $g.FillPath($mark,$bar)
            $bar.Dispose()
        }
    } elseif ($state -eq 'released') {
        $points = [Drawing.PointF[]]@([Drawing.PointF]::new(25,20),[Drawing.PointF]::new(43,32),[Drawing.PointF]::new(25,44))
        $g.FillPolygon($mark,$points)
    } else {
        $g.FillRectangle($mark,28.5,19,7,18)
        $g.FillEllipse($mark,28.5,41,7,7)
    }
    $result = [Drawing.Bitmap]::new($size,$size)
    $down = [Drawing.Graphics]::FromImage($result)
    $down.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $down.DrawImage($large,0,0,$size,$size)
    $down.Dispose(); $g.Dispose(); $large.Dispose()
    $mark.Dispose(); $edge.Dispose(); $brush.Dispose(); $shield.Dispose()
    return ,$result
}

foreach($state in @('active','released','error')) {
    $previewIcon = New-GuardBitmap 256 $state
    $previewIcon.Save((Join-Path $assets "$state.png"),[Drawing.Imaging.ImageFormat]::Png)
    $previewIcon.Dispose()
    # Only the main desktop icon needs the large Explorer sizes.
    $sizes = if($state -eq 'active'){@(16,20,24,32,48,64,128,256)}else{@(16,20,24,32,48,64)}
    $images = @(foreach($size in $sizes) {
        $bitmap = New-GuardBitmap $size $state
        $stream = [IO.MemoryStream]::new()
        $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
        [pscustomobject]@{Size=$size;Data=$stream.ToArray()}
        $stream.Dispose(); $bitmap.Dispose()
    })
    $file=[IO.File]::Create((Join-Path $assets "$state.ico"))
    $writer=[IO.BinaryWriter]::new($file)
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset=6+16*$images.Count
    foreach($item in $images){
        $dimension=if($item.Size -eq 256){0}else{$item.Size}
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$item.Data.Length); $writer.Write([uint32]$offset)
        $offset+=$item.Data.Length
    }
    foreach($item in $images){$writer.Write([byte[]]$item.Data)}
    $writer.Dispose(); $file.Dispose()
}

$preview=[Drawing.Bitmap]::new(660,250)
$canvas=[Drawing.Graphics]::FromImage($preview)
$canvas.Clear([Drawing.ColorTranslator]::FromHtml('#F4F7FA'))
$font=[Drawing.Font]::new('Segoe UI',11)
$textBrush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#334155'))
$i=0
foreach($state in @('active','released','error')){
    $large=New-GuardBitmap 128 $state
    $small=New-GuardBitmap 16 $state
    $canvas.DrawImage($large,46+220*$i,20)
    $canvas.DrawImage($small,73+220*$i,170)
    $canvas.DrawString($state,$font,$textBrush,97+220*$i,167)
    $canvas.FillRectangle([Drawing.Brushes]::MidnightBlue,46+220*$i,207,128,30)
    $canvas.DrawImage($small,102+220*$i,214)
    $large.Dispose();$small.Dispose();$i++
}
$preview.Save((Join-Path $assets 'preview.png'),[Drawing.Imaging.ImageFormat]::Png)
$textBrush.Dispose();$font.Dispose();$canvas.Dispose();$preview.Dispose()
Get-ChildItem -LiteralPath $assets -File | Select-Object Name,Length
