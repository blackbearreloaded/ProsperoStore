<#
  ps5-native-app-boilerplate - Windows build entry point.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Runs the canonical Linux/WSL build so both host entry points share one
  metadata parser and one packaging pipeline.
#>

#requires -Version 5.1
param(
    [ValidateSet("Folder", "Ffpkg")]
    [string]$OutputFormat = "Folder",
    [switch]$Ffpkg
)

$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    throw "ps5-native-app-boilerplate: $Message"
}

function Convert-ToWslPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if ($absolute -notmatch '^([A-Za-z]):\\(.*)$') {
        Fail "The repository must be on a Windows drive visible to WSL."
    }
    return "/mnt/$($Matches[1].ToLowerInvariant())/$($Matches[2].Replace('\', '/'))"
}

if ($Ffpkg) {
    if ($OutputFormat -notin @("Folder", "Ffpkg")) {
        Fail "-Ffpkg cannot be combined with -OutputFormat $OutputFormat."
    }
    $OutputFormat = "Ffpkg"
}
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Fail "WSL was not found. Install WSL and a Linux distribution first."
}

$build = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "tools/build.sh"
$arguments = @("--exec", "env")
foreach ($name in @(
        "APP_DEFINITIONS",
        "APP_INCLUDE_PATHS",
        "APP_STATIC_ARCHIVES",
        "APP_RUNTIME_MODULES",
        "PACBREW_PACKAGES",
        "PACBREW_INCLUDE_PATHS",
        "PACBREW_STATIC_ARCHIVES")) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($null -ne $value) {
        $arguments += "${name}=$value"
    }
}
$arguments += @("bash", (Convert-ToWslPath $build), $OutputFormat)
& wsl.exe @arguments
if ($LASTEXITCODE -ne 0) {
    Fail "The WSL build failed."
}
