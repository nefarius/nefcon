param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^v\d+\.\d+\.\d+$")]
    [string]$Tag,

    [Parameter(Mandatory = $true)]
    [string]$CertificateSubjectName,

    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [string]$WorkspaceRoot = (Join-Path $PSScriptRoot ".."),
    [string]$OutputDir = (Join-Path $PSScriptRoot "../.release-local"),
    [string]$AppVeyorAccount = "nefarius",
    [string]$AppVeyorProject = "nefcon",
    [switch]$NoPublish
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:AppVeyorApiBase = "https://ci.appveyor.com/api"

function Ensure-WdkWhere {
    $command = Get-Command wdkwhere -ErrorAction SilentlyContinue
    if (!$command) {
        throw "wdkwhere was not found in PATH. Install it first (dotnet tool install --global Nefarius.Tools.WDKWhere)."
    }

    return $command.Source
}

function Get-AppVeyorBuildForTag {
    param(
        [string]$Account,
        [string]$Project,
        [string]$TagValue
    )

    $url = "$script:AppVeyorApiBase/projects/$Account/$Project/branch/$TagValue"
    Write-Host "Querying AppVeyor: $url"

    try {
        $response = Invoke-RestMethod -Uri $url -Method Get -ContentType "application/json"
    }
    catch {
        throw "Failed to retrieve AppVeyor build for tag '$TagValue': $_"
    }

    $build = $response.build
    if (!$build) {
        throw "No build object returned for tag '$TagValue'."
    }

    if ($build.status -ne "success") {
        throw "Build for tag '$TagValue' has status '$($build.status)', expected 'success'."
    }

    $jobs = $build.jobs
    if (!$jobs -or $jobs.Count -eq 0) {
        throw "Build for tag '$TagValue' has no jobs."
    }

    $failed = $jobs | Where-Object { $_.status -ne "success" }
    if ($failed) {
        $names = ($failed | ForEach-Object { $_.name }) -join ", "
        throw "Not all jobs succeeded. Failed: $names"
    }

    Write-Host "Found build $($build.version) with $($jobs.Count) successful job(s)."
    return $jobs
}

function Save-AppVeyorArtifacts {
    param(
        [array]$Jobs,
        [string]$DestinationDir
    )

    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null

    foreach ($job in $Jobs) {
        $jobId = $job.jobId
        $jobName = $job.name
        Write-Host "Listing artifacts for job '$jobName' ($jobId)..."

        $artifacts = Invoke-RestMethod -Uri "$script:AppVeyorApiBase/buildjobs/$jobId/artifacts" -Method Get -ContentType "application/json"

        if (!$artifacts -or $artifacts.Count -eq 0) {
            throw "No artifacts found for job '$jobName'."
        }

        foreach ($artifact in $artifacts) {
            $remotePath = $artifact.fileName

            $sanitized = $remotePath -replace '[\\/]+', '/'
            $sanitized = $sanitized.TrimStart('/')
            if ($sanitized -match '(^|/)\.\.(/|$)') {
                throw "Artifact fileName contains path traversal: '$remotePath'"
            }

            $localPath = Join-Path $DestinationDir $sanitized
            $resolvedLocal = [System.IO.Path]::GetFullPath($localPath)
            $resolvedDest = [System.IO.Path]::GetFullPath($DestinationDir)
            if (!$resolvedLocal.StartsWith($resolvedDest + [System.IO.Path]::DirectorySeparatorChar)) {
                throw "Artifact path escapes destination directory: '$remotePath'"
            }

            $localDir = Split-Path $resolvedLocal -Parent
            New-Item -ItemType Directory -Path $localDir -Force | Out-Null

            $downloadUrl = "$script:AppVeyorApiBase/buildjobs/$jobId/artifacts/$remotePath"
            Write-Host "  Downloading $remotePath ..."
            Invoke-WebRequest -Uri $downloadUrl -OutFile $resolvedLocal -UseBasicParsing
        }
    }
}

function Sign-Binary {
    param(
        [string]$WdkWherePath,
        [string]$CertSubjectName,
        [string]$Timestamp,
        [string]$FilePath
    )

    if (!(Test-Path $FilePath)) {
        throw "Expected binary missing: $FilePath"
    }

    & $WdkWherePath run signtool sign /n $CertSubjectName /a /fd SHA256 /td SHA256 /tr $Timestamp $FilePath
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for '$FilePath' with exit code $LASTEXITCODE."
    }
}

Push-Location $WorkspaceRoot
try {
    gh auth status | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub CLI is not authenticated. Run 'gh auth login' first."
    }

    $wdkWhere = Ensure-WdkWhere
    Write-Host "Using wdkwhere: $wdkWhere"

    $resolvedOutputDir = Resolve-Path (New-Item -ItemType Directory -Path $OutputDir -Force)
    $workRoot = Join-Path $resolvedOutputDir ".work-$Tag"
    if (Test-Path $workRoot) {
        Remove-Item -Path $workRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

    $jobs = Get-AppVeyorBuildForTag -Account $AppVeyorAccount -Project $AppVeyorProject -TagValue $Tag

    $downloadDir = Join-Path $workRoot "unsigned"
    Save-AppVeyorArtifacts -Jobs $jobs -DestinationDir $downloadDir

    $targets = @(Get-ChildItem -Path $downloadDir -Filter "*.exe" -Recurse)
    if ($targets.Count -eq 0) {
        throw "No exe files found after downloading artifacts."
    }

    foreach ($file in $targets) {
        Write-Host "Signing $($file.FullName)"
        Sign-Binary -WdkWherePath $wdkWhere -CertSubjectName $CertificateSubjectName -Timestamp $TimestampUrl -FilePath $file.FullName
    }

    # The artifact paths start with bin\<platform>\, strip the leading "bin" level
    # so the zip contains x86/, x64/, ARM64/ at the root.
    $binRoot = Join-Path $downloadDir "bin"
    if (!(Test-Path $binRoot)) {
        throw "Expected directory '$binRoot' not found. Artifact layout may have changed."
    }

    $finalZip = Join-Path $resolvedOutputDir "nefcon_x86_amd64_arm64.zip"
    if (Test-Path $finalZip) {
        Remove-Item -Path $finalZip -Force
    }
    Compress-Archive -Path (Join-Path $binRoot "*") -DestinationPath $finalZip
    Write-Host "Created signed zip: $finalZip"

    $releaseExists = $true
    gh release view $Tag --json tagName 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $releaseExists = $false
    }

    if (!$releaseExists) {
        Write-Host "Creating draft release for '$Tag'..."
        gh release create $Tag --draft --title $Tag --generate-notes | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create draft release for '$Tag'."
        }
    }

    gh release upload $Tag $finalZip --clobber | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to upload asset to release '$Tag'."
    }
    Write-Host "Uploaded asset to release '$Tag'."

    if (-not $NoPublish) {
        gh release edit $Tag --draft=false | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to publish release '$Tag'."
        }
        Write-Host "Published release '$Tag'."
    }
    else {
        Write-Host "Draft release left unpublished due to -NoPublish."
    }
}
finally {
    Pop-Location
}
