param(
    [string]$Version = (Get-Date -Format "yyyyMMdd")
)

$ErrorActionPreference = "Stop"

$rootDir = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $rootDir "build\vs2022-x64\Release\remotepad.exe"
$releaseDir = Join-Path $rootDir "release"
$archive = Join-Path $releaseDir "remotepad-${Version}-win-x64.zip"

if (!(Test-Path $binary)) {
    Write-Error "Binary not found at $binary`nBuild first: cmake --build --preset vs2022-x64-vcpkg-release"
    exit 1
}

$staging = Join-Path $releaseDir "staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item $binary $staging
Copy-Item (Join-Path $rootDir "LICENSE") $staging
Copy-Item (Join-Path $rootDir "readme.md") $staging

if (Test-Path $archive) { Remove-Item $archive -Force }
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
Compress-Archive -Path "$staging\*" -DestinationPath $archive

Remove-Item $staging -Recurse -Force

Write-Host "Release: $archive"
