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
$neflibRootDir = Join-Path $scriptDir "neflib"
$neflibIncludeDir = Join-Path $neflibRootDir "include"
$neflibSrcDir = Join-Path $neflibRootDir "src"
$neflibVcxproj = Join-Path $neflibSrcDir "neflib.vcxproj"
$portFilePath = Join-Path $scriptDir "ports\neflib\portfile.cmake"

if (-not (Test-Path $neflibIncludeDir -PathType Container) -or -not (Test-Path $neflibSrcDir -PathType Container)) {
    Write-Error "neflib submodule not found at '$scriptDir\neflib'. Run 'git submodule update --init neflib' first."
    exit 1
}

if (-not (Test-Path $neflibVcxproj -PathType Leaf)) {
    Write-Error "neflib\src\neflib.vcxproj not found. Run 'git submodule update --init neflib' first."
    exit 1
}

if (-not (Test-Path $portFilePath -PathType Leaf)) {
    Write-Error "ports\neflib\portfile.cmake not found."
    exit 1
}

# Headers/sources/project files (recursive) plus the manifest and the MSBuild
# Directory.Build.props/targets at the submodule root, which are auto-imported by
# neflib.vcxproj and therefore also affect what gets built.
$files = @(
    @(Get-ChildItem -Path $neflibIncludeDir, $neflibSrcDir -Recurse -File |
        Where-Object { $_.Extension -in ".h", ".hpp", ".cpp", ".vcxproj", ".filters", ".json" }) +
    @(Get-ChildItem -Path $neflibRootDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in "Directory.Build.props", "Directory.Build.targets" })
) | Sort-Object FullName

# Avoid depending on the Get-FileHash cmdlet (Microsoft.PowerShell.Utility), which is not
# guaranteed to be auto-loaded on every Windows PowerShell installation.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
$combinedHash = ""

foreach ($file in $files) {
    $relativePath = $file.FullName.Substring($scriptDir.Length).TrimStart("\", "/")
    $fileBytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $fileHash = [System.BitConverter]::ToString($sha256.ComputeHash($fileBytes)).Replace("-", "")
    $combinedHash += "$relativePath|$fileHash`n"
}

$bytes = [System.Text.Encoding]::UTF8.GetBytes($combinedHash)
$finalHash = [System.BitConverter]::ToString($sha256.ComputeHash($bytes)).Replace("-", "").ToLowerInvariant()

$content = Get-Content -Path $portFilePath -Raw
$markerMatches = [regex]::Matches($content, "# source-hash: ([0-9a-f]{64})")

if ($markerMatches.Count -ne 1) {
    Write-Error "ports\neflib\portfile.cmake must contain exactly one '# source-hash: <64 hex chars>' marker, found $($markerMatches.Count). Cannot stamp the source hash."
    exit 1
}

$currentHash = $markerMatches[0].Groups[1].Value

if ($currentHash -eq $finalHash) {
    Write-Host "portfile.cmake source-hash already up to date ($finalHash), no changes made."
}
else {
    $match = $markerMatches[0]
    $updated = $content.Substring(0, $match.Index) + "# source-hash: $finalHash" + $content.Substring($match.Index + $match.Length)

    if ($updated -notmatch "# source-hash: $finalHash") {
        Write-Error "Failed to stamp ports\neflib\portfile.cmake with the new source-hash."
        exit 1
    }

    Set-Content -Path $portFilePath -Value $updated -NoNewline
    Write-Host "Stamped ports\neflib\portfile.cmake with source-hash $finalHash (from $($files.Count) files)."
}
