# Downloads the pinned Ashita SDK headers into dependencies/sdk, needed before building.
$ErrorActionPreference = 'Stop'
$rootPath = $PSScriptRoot
$commit = '4171c74c8ddb2ca2a31654f199e6c1cee40d7256'
$tree = Invoke-RestMethod -Uri ('https://api.github.com/repos/AshitaXI/Ashita-v4beta/git/trees/' + $commit + '?recursive=1')
$sdkFiles = @($tree.tree | Where-Object { $_.path -like 'plugins/sdk/*' -and $_.type -eq 'blob' -and $_.path -notlike '*/lib/*' })
foreach ($entry in $sdkFiles) {
    $relative = $entry.path.Substring('plugins/sdk/'.Length)
    $destination = Join-Path $rootPath ('dependencies/sdk/' + $relative)
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Invoke-WebRequest -Uri ('https://raw.githubusercontent.com/AshitaXI/Ashita-v4beta/' + $commit + '/' + $entry.path) -OutFile $destination
}
Set-Content -LiteralPath (Join-Path $rootPath 'dependencies/SDK-SOURCE.txt') -Value "Official Ashita SDK headers only. Source: https://github.com/AshitaXI/Ashita-v4beta/tree/$commit/plugins/sdk`nCommit: $commit`nNo addon or plugin implementation downloaded."
Write-Output "Fetched $($sdkFiles.Count) official SDK headers at $commit."
