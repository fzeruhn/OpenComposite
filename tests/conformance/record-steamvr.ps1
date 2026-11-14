#Requires -Version 7.2
<#
.SYNOPSIS
    Records SteamVR's own answers to every conformance call, with SteamVR's null driver (no headset needed), as the
    reference OpenLamina is compared with (expected\steamvr-<version>.json).
.DESCRIPTION
    1. Finds SteamVR: the paths file's runtime entry ending in \SteamVR, else Steam's steamapps\common\SteamVR.
    2. Refuses while SteamVR, Monado, Pimax's service or OVR Lighthouse Manager runs.
    3. Copies, with their SHA-256s, the paths file (%LOCALAPPDATA%\openvr\openvrpaths.vrpath) and, from Steam's config
       folder, steamvr.vrsettings, chaperone_info.vrchap, appconfig.json and vrappconfig\.
    4. Turns on the null driver in steamvr.vrsettings (driver_null.enable, steamvr.forcedDriver = null,
       steamvr.activateMultipleDrivers = false, other keys kept) and puts SteamVR first in the paths file.
    5. Runs run.py against SteamVR's vrclient_x64.dll (--real-runtime: unsafe_methods.txt isn't called).
    6. Always, at the end: stops SteamVR's processes, puts every backed-up file back (and removes any of them that
       didn't exist before), and fails unless every SHA-256 matches its backup.
.EXAMPLE
    tests\conformance\record-steamvr.ps1 -Conformance build-tests\tests\oc_conformance.exe -Out out\steamvr
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Conformance,
    [Parameter(Mandatory)] [string]$Out,
    [switch]$WhatIf,
    # Only the init-matrix case, and no expected file: to check SteamVR starts with the null driver
    [switch]$Probe,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

# 1. SteamVR and Steam's config folder
$pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
$steamvr = $null
if (Test-Path -LiteralPath $pathsFile) {
    $paths = Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json
    $steamvr = $paths.runtime | Where-Object { $_ -match '[\\/]SteamVR[\\/]?$' -and (Test-Path (Join-Path $_ 'bin\vrclient_x64.dll')) } | Select-Object -First 1
}
$steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
if (-not $steamvr -and $steam) {
    $candidate = Join-Path $steam 'steamapps\common\SteamVR'
    if (Test-Path (Join-Path $candidate 'bin\vrclient_x64.dll')) { $steamvr = $candidate }
}
if (-not $steamvr) { throw 'SteamVR not found' }
if (-not $steam) { throw "Steam's folder not found (HKCU\Software\Valve\Steam SteamPath)" }
$config = Join-Path $steam 'config'
$version = (Get-Content -LiteralPath (Join-Path $steamvr 'bin\version.txt') -Raw).Trim()
Write-Host "SteamVR $version at $steamvr; Steam config $config"

# 2. Nothing VR running
$running = Get-Process -Name vrserver, vrmonitor, vrcompositor, monado-service, pi_server, OVRLighthouseManager -ErrorAction SilentlyContinue
if ($running) { throw "Close these first: $(($running.Name | Sort-Object -Unique) -join ', ')" }

# 3. The backup
$backup = Join-Path $Out 'backup'
$files = @($pathsFile) + @('steamvr.vrsettings', 'chaperone_info.vrchap', 'appconfig.json' | ForEach-Object { Join-Path $config $_ })
if (Test-Path (Join-Path $config 'vrappconfig')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $config 'vrappconfig') -File | ForEach-Object FullName
}
$manifest = foreach ($f in $files) {
    [pscustomobject]@{
        Path   = $f
        Exists = Test-Path -LiteralPath $f
        Hash   = if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash } else { $null }
        Copy   = Join-Path $backup ([IO.Path]::GetFileName($f) + '.' + [Math]::Abs($f.GetHashCode()))
    }
}
$appconfigDir = Join-Path $config 'vrappconfig'
$appconfigBefore = if (Test-Path $appconfigDir) { @(Get-ChildItem -LiteralPath $appconfigDir -File | ForEach-Object FullName) } else { @() }

if ($WhatIf) {
    $manifest | Format-Table Exists, Hash, Path -AutoSize | Out-String -Width 220 | Write-Host
    Write-Host "Would set driver_null.enable, steamvr.forcedDriver = null, steamvr.activateMultipleDrivers = false in $(Join-Path $config 'steamvr.vrsettings')"
    Write-Host "Would run run.py against $(Join-Path $steamvr 'bin\vrclient_x64.dll'), then put everything back"
    return
}

New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach ($m in $manifest) { if ($m.Exists) { Copy-Item -LiteralPath $m.Path -Destination $m.Copy -Force } }
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8

$failed = $false
try {
    # 4. The null driver
    $settingsPath = Join-Path $config 'steamvr.vrsettings'
    $settings = if (Test-Path -LiteralPath $settingsPath) { Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json -AsHashtable } else { @{} }
    if (-not $settings.ContainsKey('driver_null')) { $settings['driver_null'] = @{} }
    if (-not $settings.ContainsKey('steamvr')) { $settings['steamvr'] = @{} }
    $settings['driver_null']['enable'] = $true
    $settings['steamvr']['forcedDriver'] = 'null'
    $settings['steamvr']['activateMultipleDrivers'] = $false
    $settings | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $settingsPath -Encoding utf8

    # SteamVR first in the paths file, as SteamVR puts itself when started from Steam: its own processes (vrmonitor
    # and the like) find the runtime there, and would otherwise load whatever is first (OpenLamina, say)
    if (Test-Path -LiteralPath $pathsFile) {
        $pathsJson = Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json -AsHashtable
        $others = @($pathsJson['runtime'] | Where-Object { $_ -ne $steamvr })
        $pathsJson['runtime'] = @($steamvr) + $others
        $pathsJson | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $pathsFile -Encoding utf8
    }

    # 5. The run
    $vrclient = Join-Path $steamvr 'bin\vrclient_x64.dll'
    & python -I (Join-Path $here 'run.py') --runtime $vrclient --conformance $Conformance --xr-runtime none `
        --real-runtime --allow-failures --jobs $Jobs --out $Out @($Probe ? @('--cases', 'init-matrix') : @())
    if ($LASTEXITCODE -ne 0) { throw "run.py failed ($LASTEXITCODE)" }
}
catch {
    $failed = $true
    Write-Warning $_
}
finally {
    # 6. SteamVR stopped, every file back as it was
    Get-Process -Name vrserver, vrmonitor, vrcompositor, vrdashboard, vrwebhelper, vrstartup, vrprismhost -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
    $mismatch = @()
    foreach ($m in $manifest) {
        if ($m.Exists) {
            Copy-Item -LiteralPath $m.Copy -Destination $m.Path -Force
            if ((Get-FileHash -LiteralPath $m.Path -Algorithm SHA256).Hash -ne $m.Hash) { $mismatch += $m.Path }
        }
        elseif (Test-Path -LiteralPath $m.Path) {
            Remove-Item -LiteralPath $m.Path -Force
        }
    }
    if (Test-Path $appconfigDir) {
        Get-ChildItem -LiteralPath $appconfigDir -File | Where-Object { $appconfigBefore -notcontains $_.FullName } |
            ForEach-Object { Write-Host "Removing $($_.FullName), made during the run"; Remove-Item -LiteralPath $_.FullName -Force }
    }
    if ($mismatch) { throw "NOT put back exactly (backups in $backup): $($mismatch -join ', ')" }
    Write-Host "Every file put back; SHA-256s match ($backup)."
}
if ($failed) { throw 'The recording failed; files were put back.' }

# 7. The expected answers
if ($Probe) {
    Get-Content -LiteralPath (Join-Path $Out 'raw\case-init-matrix.jsonl') | Write-Host
    return
}
$results = Get-Content -LiteralPath (Join-Path $Out 'results.json') -Raw
$target = Join-Path $here "expected\steamvr-$version.json"
New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
Set-Content -LiteralPath $target -Value $results -Encoding utf8
Write-Host "Wrote $target"
