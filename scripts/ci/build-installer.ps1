#Requires -Version 7.2
<#
.SYNOPSIS
    Builds OpenComposite-Setup.exe, the Windows installer (installer\OpenComposite.iss), the way the release workflow
    (.github/workflows/build.yml) does.
.DESCRIPTION
    From the Windows builds' DLLs: -Artifacts is a folder with windows-x64\vrclient_x64.dll and windows-x86\vrclient.dll,
    as the workflow's artifacts are, or as build-windows.ps1 leaves them when copied there.

    1. scripts\ci\package.py lays out the OpenComposite folder as OpenComposite-windows.zip has it (bin\, the licences,
       SOURCE.txt with -Version).
    2. RuntimeSwitcher\Tests runs, then RuntimeSwitcher builds OpenComposite.exe; warnings are errors.
    3. Inno Setup compiles installer\OpenComposite.iss to <Out>\OpenComposite-Setup.exe.

    Needs the .NET SDK 8 or later, Python, and Inno Setup 6 (winget install JRSoftware.InnoSetup).
.EXAMPLE
    scripts\ci\build-installer.ps1 -Artifacts artifacts -Version 2026.11.06
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Artifacts,
    [Parameter(Mandatory)] [string]$Version,
    [string]$Out,
    [string]$Repository = 'fzeruhn/OpenComposite'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Out) {
    $Out = Join-Path $root 'build\installer'
}
# Absolute: Inno Setup reads relative paths from the .iss file's folder.
$Out = [IO.Path]::GetFullPath($Out, (Get-Location).Path)
$Artifacts = (Resolve-Path -LiteralPath $Artifacts).Path

function Invoke-Checked([scriptblock]$command, [string]$what) {
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed (exit code $LASTEXITCODE)."
    }
}

# Windows' four-number file version: 2026.11.06 is 2026.11.6.0; a version without numbers (dev-...) is 0.0.0.0.
$numeric = '0.0.0.0'
if ($Version -match '^(\d+(?:\.\d+){0,3})(?:-[0-9A-Za-z.-]+)?$') {
    $parts = @($Matches[1].Split('.') | ForEach-Object { [long]$_ })
    if (-not ($parts | Where-Object { $_ -gt 65535 })) {
        $numeric = (@($parts) + @(0, 0, 0, 0))[0..3] -join '.'
    }
}

$onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
$iscc = @(
    $(if ($onPath) { $onPath.Source })
    Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'
    Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $iscc) {
    throw 'Inno Setup 6 is needed (winget install JRSoftware.InnoSetup).'
}

$folder = Join-Path $Out 'OpenComposite'
$switcher = Join-Path $Out 'switcher'
# Only what this script makes in $Out is deleted; anything else there is left alone.
foreach ($path in $folder, $switcher, (Join-Path $Out 'OpenComposite-Setup.exe')) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# 1. The OpenComposite folder.
Invoke-Checked {
    python (Join-Path $root 'scripts\ci\package.py') --version $Version --artifacts $Artifacts --windows-folder $folder `
        --repository $Repository
} 'Laying out the OpenComposite folder'

# 2. OpenComposite.exe, after its tests.
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
Invoke-Checked { dotnet test (Join-Path $root 'RuntimeSwitcher\Tests') --nologo } 'The RuntimeSwitcher tests'
Invoke-Checked {
    dotnet build (Join-Path $root 'RuntimeSwitcher\RuntimeSwitcher.csproj') --nologo -c Release -o $switcher `
        "-p:Version=$numeric" "-p:InformationalVersion=$Version" -p:ContinuousIntegrationBuild=true
} 'Building OpenComposite.exe'
# Newtonsoft.Json.dll goes with it, so its licence does too.
$packages = ((dotnet nuget locals global-packages --list) -replace '^global-packages:\s*', '').Trim()
$jsonVersion = ([xml](Get-Content -LiteralPath (Join-Path $root 'RuntimeSwitcher\RuntimeSwitcher.csproj') -Raw)).Project.ItemGroup.PackageReference |
    Where-Object Include -EQ 'Newtonsoft.Json' | ForEach-Object Version
$jsonLicence = Join-Path $packages "newtonsoft.json\$jsonVersion\LICENSE.md"
if (-not (Test-Path -LiteralPath $jsonLicence)) {
    throw "$jsonLicence is missing."
}
Copy-Item -LiteralPath $jsonLicence -Destination (Join-Path $switcher 'LICENSE_Newtonsoft.Json.txt')

# 3. The installer.
Invoke-Checked {
    & $iscc /Q "/DAppVersion=$Version" "/DNumericVersion=$numeric" "/DFolder=$folder" "/DSwitcher=$switcher" "/DOutput=$Out" `
        (Join-Path $root 'installer\OpenComposite.iss')
} 'Compiling the installer'

$setup = Join-Path $Out 'OpenComposite-Setup.exe'
Write-Host "OpenComposite $Version ($numeric), Newtonsoft.Json $jsonVersion"
Write-Host "  $setup ($([math]::Round((Get-Item -LiteralPath $setup).Length / 1KB)) KB)"
