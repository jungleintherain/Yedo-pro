param([string]$Assets, [string]$Baseline, [string]$BuildId = '', [string]$Out = '', [switch]$WriteBaseline)
# YEdo Mod Pro: in-game updater patches.
#  -WriteBaseline : remember every file of the APK that was just built (size + time) in $Baseline
#  otherwise      : zip every file that changed since that APK into $Out (+ yedo_patch.json)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Assets).Path.TrimEnd('\')
$skip = @('config/yedo_build.txt', 'config/yedo_patch_installed.txt')
function Get-State {
	$map = @{}
	Get-ChildItem -LiteralPath $root -Recurse -File -Force | ForEach-Object {
		$rel = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
		if ($skip -notcontains $rel) { $map[$rel] = "{0}`t{1}" -f $_.Length, $_.LastWriteTimeUtc.Ticks }
	}
	return $map
}
$enc = New-Object System.Text.UTF8Encoding $false
$now = Get-State
if ($WriteBaseline) {
	$lines = $now.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}`t{1}" -f $_.Name, $_.Value }
	[System.IO.File]::WriteAllLines($Baseline, [string[]]$lines, $enc)
	Write-Output ("baseline: {0} files" -f $now.Count)
	exit 0
}
if (-not (Test-Path -LiteralPath $Baseline)) { Write-Output 'NO BASELINE - build the APK once with build-android.bat first'; exit 2 }
$base = @{}
foreach ($l in [System.IO.File]::ReadAllLines($Baseline, $enc)) {
	$p = $l.Split("`t")
	if ($p.Count -ge 3) { $base[$p[0]] = "{0}`t{1}" -f $p[1], $p[2] }
}
$changed = @($now.Keys | Where-Object { -not $base.ContainsKey($_) -or $base[$_] -ne $now[$_] } | Sort-Object)
$deleted = @($base.Keys | Where-Object { -not $now.ContainsKey($_) } | Sort-Object)
$version = Get-Date -Format 'yyyyMMdd-HHmmss'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
$zip = [System.IO.Compression.ZipFile]::Open($Out, 'Create')
try {
	foreach ($rel in $changed) {
		[void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $root $rel), $rel, 'Optimal')
	}
	$info = [ordered]@{ base = $BuildId; version = $version; files = $changed.Count; delete = $deleted }
	$entry = $zip.CreateEntry('yedo_patch.json')
	$w = New-Object System.IO.StreamWriter($entry.Open(), $enc)
	$w.Write(($info | ConvertTo-Json -Depth 3))
	$w.Close()
} finally { $zip.Dispose() }
$mb = [math]::Round((Get-Item -LiteralPath $Out).Length / 1MB, 2)
Write-Output ("PATCH {0}: {1} changed, {2} deleted, {3} MB" -f $version, $changed.Count, $deleted.Count, $mb)
