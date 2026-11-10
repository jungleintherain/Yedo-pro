param([string]$Assets)
# Builds index.txt (folders) and filelist.txt (folders + files) for the Android asset copier.
$root = Join-Path $Assets 'defaults'
$enc = New-Object System.Text.UTF8Encoding $false
$items = Get-ChildItem -LiteralPath $root -Recurse -Force
$rel = { param($p) $p.Substring($root.Length + 1).Replace('\', '/') }
$dirs = @($items | Where-Object { $_.PSIsContainer } | ForEach-Object { & $rel $_.FullName })
$all = @($items | ForEach-Object { & $rel $_.FullName })
[System.IO.File]::WriteAllLines((Join-Path $Assets 'index.txt'), [string[]]$dirs, $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'filelist.txt'), [string[]]$all, $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'indexu.txt'), [string[]]@(), $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'filelistu.txt'), [string[]]@(), $enc)
Write-Output ("asset list: {0} folders, {1} entries" -f $dirs.Count, $all.Count)
