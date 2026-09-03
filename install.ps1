# Rook Toolchain Installer for Windows
param(
    [string]$Prefix = "$env:USERPROFILE\bin\Rook",
    [switch]$WithZed,
    [switch]$NoZed,
    [switch]$Help
)

if ($Help) {
    Write-Host "Rook Toolchain Installer for Windows"
    Write-Host "Usage: .\install.ps1 [-Prefix <dir>] [-WithZed] [-NoZed]"
    exit 0
}

Write-Host "=========================================="
Write-Host "       Rook Toolchain Installer (Windows) "
Write-Host "=========================================="
Write-Host "Target Prefix: $Prefix"

# 1. Check prerequisites
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake is required but not installed or not on PATH."
    exit 1
}

# 2. Build
Write-Host "Building rokade and rook-lsp..."
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 3. Create destination directories
$binDir = Join-Path $Prefix "bin"
$stdDir = Join-Path $Prefix "std"
$shareDir = Join-Path $Prefix "share\rook"
$zedDir = Join-Path $Prefix "editors\zed"

New-Item -ItemType Directory -Force -Path $binDir | Out-Null
New-Item -ItemType Directory -Force -Path $stdDir | Out-Null
New-Item -ItemType Directory -Force -Path $shareDir | Out-Null
New-Item -ItemType Directory -Force -Path $zedDir | Out-Null

# 4. Copy files
$exePath = "build\rokade.exe"
if (-not (Test-Path $exePath)) { $exePath = "build\Release\rokade.exe" }
if (Test-Path $exePath) {
    Copy-Item -Force $exePath (Join-Path $binDir "rokade.exe")
}

$lspPath = "build\rook-lsp.exe"
if (-not (Test-Path $lspPath)) { $lspPath = "build\Release\rook-lsp.exe" }
if (Test-Path $lspPath) {
    Copy-Item -Force $lspPath (Join-Path $binDir "rook-lsp.exe")
}

Copy-Item -Recurse -Force "std\*" $stdDir
Copy-Item -Force "src\libc\commandlist.json" (Join-Path $shareDir "commandlist.json")
Copy-Item -Recurse -Force "editors\zed\*" $zedDir

Write-Host "Installed files to $Prefix"

# 5. Add to PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$binDir*") {
    Write-Host "Adding $binDir to User PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$binDir", "User")
}

# 6. Zed Detection
$zedLocal = Join-Path $env:LOCALAPPDATA "Zed"
if (Test-Path $zedLocal -or (Get-Command zed -ErrorAction SilentlyContinue) -or (Get-Command zeditor -ErrorAction SilentlyContinue)) {
    Write-Host "Zed Editor detected."
    if (-not $NoZed) {
        $zedExtDir = Join-Path $env:LOCALAPPDATA "Zed\extensions\installed\rook"
        New-Item -ItemType Directory -Force -Path (Split-Path $zedExtDir) | Out-Null
        Copy-Item -Recurse -Force $zedDir $zedExtDir
        Write-Host "Zed extension installed to $zedExtDir"
    }
}

Write-Host "=========================================="
Write-Host "    Rook installation completed!          "
Write-Host "=========================================="
