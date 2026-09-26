<#
.SYNOPSIS
    Signs a file with Azure Artifact Signing (formerly Trusted Signing).

.PARAMETER FilePath
    Path to the file to sign.

.NOTES
    Needs AZURE_TENANT_ID, AZURE_CLIENT_ID and AZURE_CLIENT_SECRET in the
    environment. signing\metadata.json excludes every other credential type, so
    the chain can only use those three.

    Certificates issued by Artifact Signing are valid for three days, so the
    RFC3161 timestamp is mandatory: without it the binary stops validating
    within the week.
#>

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$FilePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$MetadataJson = Join-Path $RepoRoot 'signing\metadata.json'
if (-not (Test-Path $MetadataJson)) {
    Write-Error "ERROR: $MetadataJson does not exist."
    exit 1
}

# The client tools moved when Trusted Signing was renamed to Artifact Signing.
$DlibCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Microsoft\ArtifactSigningClientTools\Azure.CodeSigning.Dlib.dll'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft\MicrosoftArtifactSigningClientTools\Azure.CodeSigning.Dlib.dll')
)
$Dlib = $DlibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Dlib) {
    Write-Error "ERROR: Azure.CodeSigning.Dlib.dll not found. Install with: winget install -e --id Microsoft.Azure.ArtifactSigningClientTools"
    exit 1
}

# Newest SDK first rather than a pinned version. 10.0.20348 is documented as
# unsupported for signing.
$SignTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^10\.' -and $_.Name -notmatch '^10\.0\.20348' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1
if (-not $SignTool) {
    Write-Error "ERROR: signtool.exe not found. Install the Windows SDK signing tools."
    exit 1
}

foreach ($envVar in @('AZURE_TENANT_ID', 'AZURE_CLIENT_ID', 'AZURE_CLIENT_SECRET')) {
    if (-not [Environment]::GetEnvironmentVariable($envVar)) {
        Write-Error "ERROR: Environment variable $envVar is not set."
        exit 1
    }
}

$resolved = (Resolve-Path $FilePath).Path
Write-Host "--- Signing $resolved ---" -ForegroundColor Cyan

& $SignTool sign /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 `
    /dlib $Dlib /dmdf $MetadataJson $resolved
if ($LASTEXITCODE -ne 0) {
    Write-Error "ERROR: signtool sign failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

& $SignTool verify /pa $resolved
if ($LASTEXITCODE -ne 0) {
    Write-Error "ERROR: signtool verify failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "Signed and verified." -ForegroundColor Green
