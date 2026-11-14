#Requires -Version 7.2
<#
.SYNOPSIS
    Builds OpenComposite's Windows DLL (bin\vrclient_x64.dll or bin\vrclient.dll) the way the release workflow
    (.github/workflows/build.yml) does.
.DESCRIPTION
    A clean Release build with Ninja. Warnings are errors, and the C runtime is linked in (/MT): the DLL is loaded into
    games, and a game's folder needn't have the Visual C++ runtime the DLL would otherwise need.

    Needs Visual Studio or its Build Tools with the C++ x64/x86 tools and "C++ ATL" (OpenComposite includes
    atlbase.h), and Python. -Vulkan is a folder with include\vulkan and lib\vulkan-1.lib of the same architecture, such
    as vcpkg's installed\x64-windows or installed\x86-windows of vulkan-headers and vulkan-loader. They are copied to
    libs\vulkan, where CMakeLists.txt looks (Lib for x64, Lib32 for x86).

    Run it in a plain PowerShell: it enters the Visual Studio developer shell for -Arch itself.
.EXAMPLE
    scripts\ci\build-windows.ps1 -Arch x64 -Vulkan C:\vcpkg\installed\x64-windows
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateSet('x64', 'x86')] [string]$Arch,
    [Parameter(Mandatory)] [string]$Vulkan,
    [string]$Out,
    [string]$Version,
    # Also build the tests (OC_BUILD_TESTS) and run the unit tests
    [switch]$Tests
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Out) {
    $Out = Join-Path $root "build\windows-$Arch"
}
if (-not $Version) {
    $Version = "$(git -C $root rev-parse HEAD) (local build)"
}

function Invoke-Checked([scriptblock]$command, [string]$what) {
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed (exit code $LASTEXITCODE)."
    }
}

# 1. Vulkan, where CMakeLists.txt looks.
$Vulkan = (Resolve-Path -LiteralPath $Vulkan).Path
$headers = Join-Path $Vulkan 'include\vulkan'
$lib = Join-Path $Vulkan 'lib\vulkan-1.lib'
foreach ($path in $headers, $lib) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "-Vulkan $Vulkan has no $path."
    }
}
$include = Join-Path $root 'libs\vulkan\Include'
if (Test-Path -LiteralPath $include) {
    Remove-Item -LiteralPath $include -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $include | Out-Null
Copy-Item -LiteralPath $headers -Destination $include -Recurse
$video = Join-Path $Vulkan 'include\vk_video'
if (Test-Path -LiteralPath $video) {
    Copy-Item -LiteralPath $video -Destination $include -Recurse
}
$libOut = Join-Path $root ($Arch -eq 'x64' ? 'libs\vulkan\Lib' : 'libs\vulkan\Lib32')
New-Item -ItemType Directory -Force -Path $libOut | Out-Null
Copy-Item -LiteralPath $lib -Destination $libOut -Force

# 2. The Visual Studio developer shell for -Arch (CMake and Ninja come with it).
if ($env:VSCMD_ARG_TGT_ARCH) {
    if ($env:VSCMD_ARG_TGT_ARCH -ne $Arch) {
        throw "This is a Visual Studio developer shell for $env:VSCMD_ARG_TGT_ARCH. Build $Arch from a plain PowerShell."
    }
}
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ATL -property installationPath
    }
    if (-not $vs) {
        throw 'OpenComposite needs Visual Studio (or its Build Tools) with the C++ x64/x86 tools and "C++ ATL".'
    }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$Arch -host_arch=x64" | Out-Null
}

# 3. A clean build of the DLL only (OpenComposite.exe and the installer are build-installer.ps1's).
if (Test-Path -LiteralPath $Out) {
    Remove-Item -LiteralPath $Out -Recurse -Force
}
$testsFlag = $Tests ? 'ON' : 'OFF'
Invoke-Checked {
    cmake -S $root -B $Out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
        -DERROR_ON_WARNING=ON "-DOC_VERSION=$Version" "-DOC_BUILD_TESTS=$testsFlag"
} 'Configuring OpenComposite'
Invoke-Checked { cmake --build $Out --target OCOVR } 'Building OpenComposite'
if ($Tests) {
    Invoke-Checked { cmake --build $Out } 'Building the tests'
    Invoke-Checked { ctest --test-dir $Out -L unit --output-on-failure } 'Running the unit tests'
}

# 4. The DLL is of -Arch and needs no C runtime DLL.
$dll = Join-Path $Out ($Arch -eq 'x64' ? 'bin\vrclient_x64.dll' : 'bin\vrclient.dll')
if (-not (Test-Path -LiteralPath $dll)) {
    throw "The build made no $dll."
}
$bytes = [IO.File]::ReadAllBytes($dll)
$machine = [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 0x3C) + 4)
$expected = $Arch -eq 'x64' ? 0x8664 : 0x14C
if ($machine -ne $expected) {
    throw ('{0} is for machine 0x{1:X}, not {2}.' -f $dll, $machine, $Arch)
}
$crt = dumpbin /nologo /dependents $dll | Select-String -Pattern '^\s*(vcruntime|msvcp|ucrtbase|api-ms-win-crt)' -CaseSensitive:$false
if ($crt) {
    throw "$dll needs the C runtime DLLs ($(($crt | ForEach-Object { $_.Line.Trim() }) -join ', ')): it should have it linked in."
}
# The entry points of a system-wide install (vrclient) and of a per-game one (as openvr_api.dll).
$exports = dumpbin /nologo /exports $dll
foreach ($name in 'VRClientCoreFactory', 'VR_InitInternal2') {
    if (-not ($exports -match "\s$name\s*$")) {
        throw "$dll doesn't export $name."
    }
}

Write-Host "OpenComposite $Version, $Arch"
Write-Host "  $dll ($([math]::Round((Get-Item -LiteralPath $dll).Length / 1KB)) KB)"
