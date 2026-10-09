[CmdletBinding()]
param(
    [ValidateSet('release', 'debug')][string]$Preset = 'release',
    [string]$QtRoot = $env:GPLATFORM_QT_ROOT,
    [string]$VsRoot = $env:GPLATFORM_VS_ROOT,
    [string]$CMake = 'cmake',
    [ValidateSet('x64')][string]$Architecture = 'x64',
    [switch]$ConfigureOnly,
    [switch]$Check,
    [switch]$OpenVSCode,
    [string]$SourceDir,
    [string[]]$CMakeArgs = @()
)
$ErrorActionPreference = 'Stop'
$originalEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $originalEnvironment[$_.Name] = $_.Value }
$projectRoot = if ($SourceDir) { $SourceDir } else { Split-Path $PSScriptRoot -Parent }
$result = 0
Push-Location $projectRoot
try {
    $cmakeTool = Get-Command $CMake -CommandType Application -ErrorAction SilentlyContinue
    if (-not $cmakeTool) { throw "CMake was not found: $CMake. Install CMake >= 3.24 on PATH or pass -CMake <executable>." }
    $cmakePath = $cmakeTool.Source
    if ($QtRoot) { $env:GPLATFORM_QT_ROOT = $QtRoot }
    $env:GPLATFORM_TARGET_ARCH = $Architecture
    if ($VsRoot -or -not $env:VSCMD_VER -or $env:VSCMD_ARG_TGT_ARCH -ne $Architecture) {
        if (-not $VsRoot) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio discovery failed. Install VS/Build Tools with C++ tools, or set GPLATFORM_VS_ROOT.' }
            $VsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -ne 0 -or -not $VsRoot) { throw 'No Visual Studio C++ installation found. Install the C++ workload and Windows SDK, or set GPLATFORM_VS_ROOT.' }
        }
        $devCmd = Join-Path $VsRoot 'Common7/Tools/VsDevCmd.bat'
        if (-not (Test-Path -LiteralPath $devCmd -PathType Leaf)) { throw "Invalid GPLATFORM_VS_ROOT: $VsRoot. Missing Common7/Tools/VsDevCmd.bat." }
        if ($devCmd -match '[%"\r\n]') { throw 'The VS path contains characters unsupported by the developer command shell.' }
        $developerEnvironment = & $env:ComSpec /d /c "`"$devCmd`" -arch=$Architecture -host_arch=x64 >nul && set"
        if ($LASTEXITCODE -ne 0) { throw "VS environment initialization failed: $devCmd" }
        foreach ($line in $developerEnvironment) {
            if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
        }
    }
    if (-not $VsRoot) { $VsRoot = $env:VSINSTALLDIR }
    $env:GPLATFORM_VS_ROOT = $VsRoot
    $missing = @()
    foreach ($name in 'cl.exe', 'link.exe', 'rc.exe', 'mt.exe') {
        if (-not (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)) { $missing += $name }
    }
    if ($missing.Count) { throw "Incomplete MSVC/Windows SDK environment: $($missing -join ', '). Install C++ tools and Windows SDK for $Architecture." }
    # Select the compiler from the environment we just prepared. A GCC/Clang
    # executable earlier on PATH must not change this MSVC launcher into a
    # different toolchain.
    $env:CC = (Get-Command cl.exe -CommandType Application).Source
    $env:CXX = $env:CC
    $env:PATH = "$(Split-Path $cmakePath -Parent);$env:PATH"
    Write-Host "MSVC environment: $env:VSINSTALLDIR ($env:VSCMD_ARG_TGT_ARCH)"
    if ($OpenVSCode) {
        & code --new-window $projectRoot
        if ($LASTEXITCODE -ne 0) { throw 'VS Code could not be started. Install its code command on PATH.' }
    } else {
        $configurePreset = $Preset
        if ($Check) { $configurePreset = 'check' }
        $configureArgs = @('--preset', $configurePreset)
        $configureArgs += $CMakeArgs
        & $cmakePath @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
        if (-not $ConfigureOnly -and -not $Check) {
            & $cmakePath --build --preset $Preset
            if ($LASTEXITCODE -ne 0) { throw "GPlatform build failed ($LASTEXITCODE)." }
        }
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $result = 1
} finally {
    Pop-Location
    Get-ChildItem Env: | Where-Object { -not $originalEnvironment.ContainsKey($_.Name) } | ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process') }
    foreach ($name in $originalEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $originalEnvironment[$name], 'Process') }
}
exit $result
