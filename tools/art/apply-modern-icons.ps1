# Renders modern-icons.html (monochrome icon set, tinted with the theme colors at runtime) with headless Edge into
# data/icons/modern/<size>x<size>/<name>.png and writes data/iconsmodern.qrc listing them.
# Preview: open modern-icons.html#preview in a browser.
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$page = ([Uri](Join-Path $PSScriptRoot "modern-icons.html")).AbsoluteUri
$dumpfile = Join-Path $env:TEMP "strawberry-modern-icons-dump.html"
cmd /c "`"$edge`" --headless=new --disable-gpu --virtual-time-budget=20000 --dump-dom $page > `"$dumpfile`" 2>nul"
# Edge may still be finishing the file for a moment
$text = $null
for ($i = 0; $i -lt 20 -and $null -eq $text; $i++) { try { $text = [IO.File]::ReadAllText($dumpfile) } catch { Start-Sleep -Milliseconds 500 } }
Remove-Item $dumpfile
$start = $text.IndexOf("@@BEGIN@@")
$end = $text.IndexOf("@@END@@")
if ($start -lt 0 -or $end -lt 0) { "render failed"; exit 1 }

$iconsRoot = Join-Path $Root "data\icons\modern"
$files = New-Object System.Collections.Generic.List[string]
foreach ($line in $text.Substring($start + 9, $end - $start - 9).Trim() -split "`n") {
  $parts = $line.Trim() -split '\|'
  if ($parts.Count -ne 3) { continue }
  $name, $size, $b64 = $parts
  if ($b64 -eq 'ERROR') { "ERROR rendering $name $size"; continue }
  $dir = Join-Path $iconsRoot "${size}x${size}"
  [IO.Directory]::CreateDirectory($dir) | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $dir "$name.png"), [Convert]::FromBase64String($b64))
  $files.Add("icons/modern/${size}x${size}/$name.png")
}

$qrc = "<RCC>`n    <qresource prefix=`"/`">`n" + (($files | Sort-Object | ForEach-Object { "        <file>$_</file>" }) -join "`n") + "`n    </qresource>`n</RCC>`n"
[IO.File]::WriteAllText((Join-Path $Root "data\iconsmodern.qrc"), $qrc)
"written $($files.Count) icons"
