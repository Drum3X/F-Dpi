# F-Dpi icons: a shield with an arrow breaking through a barrier, in Throne's tray state colors.
# Writes res/fdpi/{Running,Proxy,Tun,Connecting,Off}.png (512 px) and res/fdpi/F-Dpi.ico (16-256 px, PNG frames).
#   powershell -ExecutionPolicy Bypass -File script\make_icons.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "res\fdpi"
New-Item -ItemType Directory -Force $out | Out-Null

# Light (left half) and base (right half) tones per tray state.
$states = [ordered]@{
    Running    = @("#7CC85A", "#2F9E2F")
    Proxy      = @("#5BC0FF", "#0A8FE8")
    Tun        = @("#FF6060", "#E00000")
    Connecting = @("#FFC24D", "#F0A000")
    Off        = @("#ABABAB", "#6E6E6E")
}

function Color([string]$hex, [int]$alpha = 255) {
    $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
    return [System.Drawing.Color]::FromArgb($alpha, $c.R, $c.G, $c.B)
}

# All geometry on a 512 grid, scaled to the requested size.
function Shield {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc(64, 56, 64, 64, 180, 90)
    $p.AddLine(96, 56, 416, 56)
    $p.AddArc(384, 56, 64, 64, 270, 90)
    $p.AddLine(448, 88, 448, 256)
    $p.AddBezier(448, 256, 448, 376, 360, 448, 256, 488)
    $p.AddBezier(256, 488, 152, 448, 64, 376, 64, 256)
    $p.CloseFigure()
    return $p
}

function RoundedBar([float]$x, [float]$y, [float]$w, [float]$h) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = $h
    $p.AddArc($x, $y, $r, $r, 90, 180)
    $p.AddArc($x + $w - $r, $y, $r, $r, 270, 180)
    $p.CloseFigure()
    return $p
}

function Arrow {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddPolygon([System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF 256, 100),
        (New-Object System.Drawing.PointF 352, 214),
        (New-Object System.Drawing.PointF 288, 214),
        (New-Object System.Drawing.PointF 288, 404),
        (New-Object System.Drawing.PointF 224, 404),
        (New-Object System.Drawing.PointF 224, 214),
        (New-Object System.Drawing.PointF 160, 214)))
    return $p
}

function Render([int]$size, [string[]]$tones) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($size / 512.0, $size / 512.0)

    $shield = Shield
    $g.FillPath((New-Object System.Drawing.SolidBrush (Color $tones[1])), $shield)
    $g.SetClip($shield)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Color $tones[0])), 0, 0, 256, 512)
    # The barrier, broken where the arrow passes; small sizes get a thicker one so it survives.
    $bar = if ($size -le 32) { 64 } else { 52 }
    $white = New-Object System.Drawing.SolidBrush (Color "#FFFFFF" 150)
    $g.FillPath($white, (RoundedBar 24 (262 - $bar / 2) 176 $bar))
    $g.FillPath($white, (RoundedBar 312 (262 - $bar / 2) 176 $bar))
    $g.ResetClip()
    $g.FillPath((New-Object System.Drawing.SolidBrush (Color "#FFFFFF")), (Arrow))
    $g.Dispose()
    return $bmp
}

function PngBytes($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return , $ms.ToArray()
}

# A classic ICO frame: 32-bit BGRA DIB, bottom-up, twice the height for the (empty) AND mask that alpha replaces.
function DibBytes($bmp) {
    $size = $bmp.Width
    $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($size * $size * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($data)
    $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4)
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $ms
    $w.Write([UInt32]40); $w.Write([Int32]$size); $w.Write([Int32]($size * 2)); $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]0); $w.Write([UInt32]($pixels.Length + $maskRow * $size)); $w.Write([Int32]0); $w.Write([Int32]0)
    $w.Write([UInt32]0); $w.Write([UInt32]0)
    for ($y = $size - 1; $y -ge 0; $y--) { $w.Write($pixels, $y * $size * 4, $size * 4) }
    $w.Write((New-Object byte[] ($maskRow * $size)))
    $w.Flush()
    return , $ms.ToArray()
}

foreach ($state in $states.Keys) {
    $bmp = Render 512 $states[$state]
    $bmp.Save((Join-Path $out "$state.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# Small frames as DIBs, which every Windows API reads; 256 px as PNG to keep the file small.
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$frames = foreach ($size in $sizes) {
    $b = Render $size $states["Running"]
    if ($size -ge 256) { , (PngBytes $b) } else { , (DibBytes $b) }
    $b.Dispose()
}
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$frames[$i].Length); $w.Write([UInt32]$offset)
    $offset += $frames[$i].Length
}
foreach ($frame in $frames) { $w.Write($frame) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $out "F-Dpi.ico"), $ico.ToArray())
Write-Host "==> icons in $out"
