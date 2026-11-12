param([string]$Assets)
# Builds index.txt (folders) and filelist.txt (folders + files) for the Android asset copier.
# Order matters: the copier works through the list from the top, so all folders come first,
# then the files the game needs to start (config, textures, fonts...), and the ~25k scripts last.
$root = Join-Path $Assets 'defaults'
$enc = New-Object System.Text.UTF8Encoding $false
$items = Get-ChildItem -LiteralPath $root -Recurse -Force
$rel = { param($p) $p.Substring($root.Length + 1).Replace('\', '/') }
$prio = @('config', 'textures', 'fonts', 'skin', 'sound', 'story', 'expansions', 'WindBot', 'deck', 'lflists')
$rank = {
	param($r)
	$top = $r.Split('/')[0]
	$i = [array]::IndexOf($prio, $top)
	if ($i -ge 0) { return $i }
	if ($top -eq 'script' -or $top -eq 'repositories') { return 50 }
	return 20
}
# aapt leaves these out of the APK (default ignoreAssetsPattern: .*, <dir>_*, *~, CVS, thumbs.db, picasa.ini, *.scc).
# Listing one of them made the copier stop there, so everything after it never reached the phone.
$packable = {
	param($r, $isDir)
	$parts = $r.Split('/')
	for ($i = 0; $i -lt $parts.Count; $i++) {
		$p = $parts[$i]
		$isLast = ($i -eq $parts.Count - 1)
		if ($p.StartsWith('.')) { return $false }
		if ((-not $isLast -or $isDir) -and $p.StartsWith('_')) { return $false }
		if ($p -match '^(CVS|thumbs\.db|picasa\.ini)$' -or $p -like '*.scc' -or $p.EndsWith('~')) { return $false }
	}
	return $true
}
$dirs = @($items | Where-Object { $_.PSIsContainer } | ForEach-Object { & $rel $_.FullName } | Where-Object { & $packable $_ $true } | Sort-Object { $_.Split('/').Count }, { $_ })
$files = @($items | Where-Object { -not $_.PSIsContainer } | ForEach-Object { & $rel $_.FullName } | Where-Object { & $packable $_ $false } | Sort-Object { & $rank $_ }, { $_ })
$all = @($dirs) + @($files)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'index.txt'), [string[]]$dirs, $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'filelist.txt'), [string[]]$all, $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'indexu.txt'), [string[]]@(), $enc)
[System.IO.File]::WriteAllLines((Join-Path $Assets 'filelistu.txt'), [string[]]@(), $enc)
Write-Output ("asset list: {0} folders, {1} entries" -f $dirs.Count, $all.Count)
