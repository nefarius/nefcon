# Stamps ports/neflib/portfile.cmake with a hash of the local neflib submodule's sources.
#
# vcpkg computes its package ABI hash from the *port files* (portfile.cmake, vcpkg.json), not
# from whatever SOURCE_PATH a portfile happens to point at. Since ports/neflib/portfile.cmake
# builds the local "neflib" submodule checkout in place rather than downloading a tagged release,
# editing neflib's sources would otherwise never invalidate the cached build - vcpkg would keep
# serving a stale neflib.lib. Run this script after every neflib source change (or just always,
# before building) so the rewritten "# source-hash:" comment forces vcpkg to rebuild.
#
# Usage: .\sync-local-neflib.ps1

$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$neflibIncludeDir = Join-Path $scriptDir "neflib\include"
$neflibSrcDir = Join-Path $scriptDir "neflib\src"
$portFilePath = Join-Path $scriptDir "ports\neflib\portfile.cmake"

if (-not (Test-Path $neflibIncludeDir) -or -not (Test-Path $neflibSrcDir)) {
    Write-Error "neflib submodule not found at '$scriptDir\neflib'. Run 'git submodule update --init neflib' first."
    exit 1
}

if (-not (Test-Path $portFilePath)) {
    Write-Error "ports\neflib\portfile.cmake not found."
    exit 1
}

$files = @(Get-ChildItem -Path $neflibIncludeDir, $neflibSrcDir -Recurse -File |
    Where-Object { $_.Extension -in ".h", ".hpp", ".cpp", ".vcxproj" } |
    Sort-Object FullName)

# Avoid depending on the Get-FileHash cmdlet (Microsoft.PowerShell.Utility), which is not
# guaranteed to be auto-loaded on every Windows PowerShell installation.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
$combinedHash = ""

foreach ($file in $files) {
    $fileBytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $fileHash = [System.BitConverter]::ToString($sha256.ComputeHash($fileBytes)).Replace("-", "")
    $combinedHash += "$($file.FullName)|$fileHash`n"
}

$bytes = [System.Text.Encoding]::UTF8.GetBytes($combinedHash)
$finalHash = [System.BitConverter]::ToString($sha256.ComputeHash($bytes)).Replace("-", "").ToLowerInvariant()

$content = Get-Content -Path $portFilePath -Raw
$updated = $content -replace "# source-hash: [0-9a-f]+", "# source-hash: $finalHash"

if ($updated -eq $content) {
    Write-Host "portfile.cmake source-hash line not found or already matches, no changes made."
}
else {
    Set-Content -Path $portFilePath -Value $updated -NoNewline
    Write-Host "Stamped ports\neflib\portfile.cmake with source-hash $finalHash (from $($files.Count) files)."
}
