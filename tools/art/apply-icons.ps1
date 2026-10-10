# Renders icons.html (colorful pastel icon set, logo, mascot, ...) with headless Edge and writes the results into the
# repository: overwrites the matching PNGs in data/icons (only icons that already exist there), the pictures listed
# in icons.html and dist/windows/strawberry.ico. Preview: open icons.html#preview in a browser.
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$page = ([Uri](Join-Path $PSScriptRoot "icons.html")).AbsoluteUri
$dumpfile = Join-Path $env:TEMP "strawberry-icons-dump.html"
cmd /c "`"$edge`" --headless=new --disable-gpu --virtual-time-budget=20000 --dump-dom $page > `"$dumpfile`" 2>nul"
# Edge may still be finishing the file for a moment
$text = $null
for ($i = 0; $i -lt 20 -and $null -eq $text; $i++) { try { $text = [IO.File]::ReadAllText($dumpfile) } catch { Start-Sleep -Milliseconds 500 } }
Remove-Item $dumpfile
$start = $text.IndexOf("@@BEGIN@@")
if ($start -lt 0) { "render failed"; exit 1 }
$end = $text.IndexOf("@@END@@", $start)
if ($end -lt 0) { "render incomplete"; exit 1 }
$written = 0; $skipped = 0; $errors = 0
$icoFrames = [ordered]@{}
foreach ($line in $text.Substring($start + 9, $end - $start - 9).Trim() -split "`n") {
  $parts = $line.Trim() -split '\|'
  if ($parts.Count -ne 3) { continue }
  $name, $size, $b64 = $parts
  if ($b64 -eq 'ERROR') { "ERROR rendering $name $size"; $errors++; continue }
  if ($name -eq '@file') {
    $bytes = [Convert]::FromBase64String($b64)
    if ($size -like 'ico:*') { $icoFrames[$size.Substring(4)] = $bytes; continue }
    [IO.File]::WriteAllBytes((Join-Path $Root $size), $bytes); $written++
    continue
  }
  $path = Join-Path $Root "data\icons\${size}x${size}\$name.png"
  if (Test-Path $path) { [IO.File]::WriteAllBytes($path, [Convert]::FromBase64String($b64)); $written++ } else { $skipped++ }
}

# Windows .ico with PNG-compressed frames
if ($icoFrames.Count -gt 0) {
  $ms = New-Object IO.MemoryStream
  $bw = New-Object IO.BinaryWriter $ms
  $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$icoFrames.Count)
  $offset = 6 + 16 * $icoFrames.Count
  foreach ($s in $icoFrames.Keys) {
    $data = $icoFrames[$s]
    $dim = if ([int]$s -ge 256) { 0 } else { [int]$s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$data.Length); $bw.Write([uint32]$offset)
    $offset += $data.Length
  }
  foreach ($s in $icoFrames.Keys) { $bw.Write($icoFrames[$s]) }
  $bw.Flush()
  [IO.File]::WriteAllBytes((Join-Path $Root "dist\windows\strawberry.ico"), $ms.ToArray())
  "ico frames: $($icoFrames.Keys -join ',')"
}
"written=$written skipped(no existing file)=$skipped errors=$errors"
