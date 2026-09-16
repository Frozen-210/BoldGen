# Regenerate both Windows icon resources from a bold serif glyph.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assetDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\BoldGen'))
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$frames = [Collections.Generic.List[byte[]]]::new()
$fontFamily = [Drawing.FontFamily]::new('Times New Roman')

try {
    foreach ($size in $sizes) {
        $scale = 4
        $edge = $size * $scale
        $bitmap = [Drawing.Bitmap]::new($edge, $edge, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $background = [Drawing.Drawing2D.GraphicsPath]::new()
        $letter = [Drawing.Drawing2D.GraphicsPath]::new()
        $matrix = [Drawing.Drawing2D.Matrix]::new()
        $brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 32, 36, 44))
        $border = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 155, 160, 168), $scale * 0.65)
        $output = $null
        $outputGraphics = $null
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $inset = $scale * 0.75
            $width = $edge - 2 * $inset
            $diameter = $edge * 0.28
            $background.AddArc($inset, $inset, $diameter, $diameter, 180, 90)
            $background.AddArc($inset + $width - $diameter, $inset, $diameter, $diameter, 270, 90)
            $background.AddArc($inset + $width - $diameter, $inset + $width - $diameter, $diameter, $diameter, 0, 90)
            $background.AddArc($inset, $inset + $width - $diameter, $diameter, $diameter, 90, 90)
            $background.CloseFigure()
            $graphics.FillPath($brush, $background)
            $graphics.DrawPath($border, $background)

            $letter.AddString('B', $fontFamily, [int][Drawing.FontStyle]::Bold, 100,
                [Drawing.PointF]::new(0, 0), [Drawing.StringFormat]::GenericTypographic)
            $bounds = $letter.GetBounds()
            $glyphScale = ($edge * 0.68) / $bounds.Height
            $matrix.Scale($glyphScale, $glyphScale)
            $letter.Transform($matrix)
            $bounds = $letter.GetBounds()
            $matrix.Reset()
            $matrix.Translate(($edge - $bounds.Width) / 2 - $bounds.X,
                ($edge - $bounds.Height) / 2 - $bounds.Y)
            $letter.Transform($matrix)
            $graphics.FillPath([Drawing.Brushes]::White, $letter)

            $output = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $outputGraphics = [Drawing.Graphics]::FromImage($output)
            $outputGraphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
            $outputGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $outputGraphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $outputGraphics.DrawImage($bitmap, [Drawing.Rectangle]::new(0, 0, $size, $size))
            $output.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
            if ($size -eq 256) {
                $output.Save((Join-Path $assetDirectory 'BoldGen-icon.png'), [Drawing.Imaging.ImageFormat]::Png)
            }
        }
        finally {
            if ($outputGraphics) { $outputGraphics.Dispose() }
            if ($output) { $output.Dispose() }
            $stream.Dispose()
            $border.Dispose()
            $brush.Dispose()
            $matrix.Dispose()
            $letter.Dispose()
            $background.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }

    foreach ($name in @('BoldGen.ico', 'small.ico')) {
        $file = [IO.File]::Create((Join-Path $assetDirectory $name))
        $writer = [IO.BinaryWriter]::new($file)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1) # ICONDIR type: icon
            $writer.Write([uint16]$sizes.Count)
            $offset = 6 + 16 * $sizes.Count
            for ($index = 0; $index -lt $sizes.Count; ++$index) {
                $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
                $writer.Write([byte]$dimension)
                $writer.Write([byte]$dimension)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]$frames[$index].Length)
                $writer.Write([uint32]$offset)
                $offset += $frames[$index].Length
            }
            foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
        }
        finally { $writer.Dispose() }
    }
}
finally { $fontFamily.Dispose() }

Write-Output 'Generated BoldGen.ico and small.ico (16 through 256 pixels), plus PNG preview.'
