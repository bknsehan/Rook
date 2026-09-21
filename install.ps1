# Rook Toolchain Installer for Windows
param(
    [string]$Prefix = "$env:USERPROFILE\bin\Rook",
    [switch]$WithExtension,
    [switch]$NoExtension,
    [string]$Editor,
    [string]$VsCodeFlavor,
    [switch]$WithZed,
    [switch]$NoZed,
    [switch]$Help
)

if ($Help) {
    Write-Host "Rook Toolchain Installer for Windows"
    Write-Host "Usage: .\install.ps1 [options]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Prefix <dir>             Installation directory (default: $env:USERPROFILE\bin\Rook)"
    Write-Host "  -WithExtension            Install editor extension(s)"
    Write-Host "  -NoExtension              Do not install any editor extension"
    Write-Host "  -Editor <name>            Editor to target: zed, vscode, or all"
    Write-Host "  -VsCodeFlavor <flavor>    VS Code variant: vscode, vscodium, code-oss, or all"
    Write-Host "  -WithZed                  Install Rook Zed editor extension (legacy alias)"
    Write-Host "  -NoZed                    Do not install Zed editor extension (legacy alias)"
    Write-Host "  -Help                     Show this help message"
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
$shareDir = Join-Path $Prefix "share\rokade"
$zedDir = Join-Path $Prefix "editors\zed"
$vscodeDir = Join-Path $Prefix "editors\vscode"

New-Item -ItemType Directory -Force -Path $binDir | Out-Null
New-Item -ItemType Directory -Force -Path $stdDir | Out-Null
New-Item -ItemType Directory -Force -Path $shareDir | Out-Null
New-Item -ItemType Directory -Force -Path $zedDir | Out-Null
New-Item -ItemType Directory -Force -Path $vscodeDir | Out-Null

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
if (Test-Path "editors\zed") {
    Copy-Item -Recurse -Force "editors\zed\*" $zedDir
}
if (Test-Path "editors\vscode") {
    Copy-Item -Recurse -Force "editors\vscode\*" $vscodeDir
}

Write-Host "Installed files to $Prefix"

# 5. Add to PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$binDir*") {
    Write-Host "Adding $binDir to User PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$binDir", "User")
}

# 6. Editor Extensions Detection & Installation
$doInstallExt = $false

if ($WithExtension -or $WithZed) {
    $doInstallExt = $true
} elseif ($NoExtension -or $NoZed) {
    $doInstallExt = $false
} else {
    # Check if running interactively
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Host ""
        $resp = Read-Host "Do you want to install editor extensions (Zed / VS Code)? [Y/n]"
        if ($resp -match "^[nN]") {
            $doInstallExt = $false
        } else {
            $doInstallExt = $true
        }
    } else {
        $doInstallExt = $true
    }
}

if ($doInstallExt) {
    $targetZed = $false
    $targetVsCode = $false

    if ($Editor) {
        switch ($Editor.ToLower()) {
            "zed" { $targetZed = $true }
            "vscode" { $targetVsCode = $true }
            default { $targetZed = $true; $targetVsCode = $true }
        }
    } elseif ($WithZed) {
        $targetZed = $true
    } elseif ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Host ""
        Write-Host "Which editor extension would you like to install?"
        Write-Host "  1) Zed"
        Write-Host "  2) VS Code (or derivatives: VSCodium, Code - OSS)"
        Write-Host "  3) Both"
        $edChoice = Read-Host "Enter choice [1-3] (default: 3)"
        switch ($edChoice) {
            "1" { $targetZed = $true }
            "2" { $targetVsCode = $true }
            default { $targetZed = $true; $targetVsCode = $true }
        }
    } else {
        $targetZed = $true
        $targetVsCode = $true
    }

    # 6a. Install Zed Extension
    if ($targetZed) {
        $zedExtDir = Join-Path $env:LOCALAPPDATA "Zed\extensions\installed\rook"
        Write-Host ""
        Write-Host ">>> Installing Rook Zed extension to $zedExtDir..."
        New-Item -ItemType Directory -Force -Path (Split-Path $zedExtDir) | Out-Null
        if (Test-Path $zedExtDir) { Remove-Item -Recurse -Force $zedExtDir }
        Copy-Item -Recurse -Force $zedDir $zedExtDir

        # Update Zed extension index.json if present
        $indexPath = Join-Path $env:LOCALAPPDATA "Zed\extensions\index.json"
        if (Test-Path $indexPath) {
            try {
                $rawJson = Get-Content $indexPath -Raw | ConvertFrom-Json
                if (-not $rawJson.extensions) {
                    $rawJson | Add-Member -NotePropertyName "extensions" -NotePropertyValue ([PSCustomObject]@{})
                }
                $manifest = [PSCustomObject]@{
                    id = "rook"
                    name = "Rook"
                    version = "0.1.0"
                    schema_version = 1
                    description = "Rook programming language support for Zed"
                    repository = "https://github.com/bknsehan/Rook"
                    authors = @("bknsehan")
                    lib = [PSCustomObject]@{ kind = "Rust"; version = "0.7.0" }
                    themes = @()
                    icon_themes = @()
                    languages = @("languages/rook")
                    grammars = [PSCustomObject]@{
                        c = [PSCustomObject]@{
                            repository = "https://github.com/tree-sitter/tree-sitter-c"
                            rev = "3efee11f784605d44623d7dadd6cd12a0f73ea92"
                            path = $null
                        }
                    }
                    language_servers = [PSCustomObject]@{
                        "rook-lsp" = [PSCustomObject]@{
                            language = "Rook"
                            languages = @("Rook")
                            language_ids = [PSCustomObject]@{}
                            code_action_kinds = $null
                        }
                    }
                    context_servers = [PSCustomObject]@{}
                    slash_commands = [PSCustomObject]@{}
                    snippets = $null
                    capabilities = @()
                }
                $extObj = [PSCustomObject]@{
                    manifest = $manifest
                    dev = $false
                }
                $rawJson.extensions | Add-Member -NotePropertyName "rook" -NotePropertyValue $extObj -Force
                $rawJson | ConvertTo-Json -Depth 10 | Set-Content $indexPath
            } catch {
                # Ignore index.json update errors
            }
        }
        Write-Host "Zed extension installed successfully!"
    }

    # 6b. Install VS Code / VSCodium / Code - OSS Extension
    if ($targetVsCode) {
        $targetCode = $false
        $targetCodium = $false
        $targetCodeOss = $false

        if ($VsCodeFlavor) {
            switch ($VsCodeFlavor.ToLower()) {
                "vscode" { $targetCode = $true }
                "vscodium" { $targetCodium = $true }
                "codium" { $targetCodium = $true }
                "code-oss" { $targetCodeOss = $true }
                "oss" { $targetCodeOss = $true }
                default { $targetCode = $true; $targetCodium = $true; $targetCodeOss = $true }
            }
        } elseif ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
            Write-Host ""
            Write-Host "Which VS Code variant do you want to target?"
            Write-Host "  1) VS Code (Official)"
            Write-Host "  2) VSCodium"
            Write-Host "  3) Code - OSS"
            Write-Host "  4) All detected variants"
            $flavorChoice = Read-Host "Enter choice [1-4] (default: 4)"
            switch ($flavorChoice) {
                "1" { $targetCode = $true }
                "2" { $targetCodium = $true }
                "3" { $targetCodeOss = $true }
                default { $targetCode = $true; $targetCodium = $true; $targetCodeOss = $true }
            }
        } else {
            $targetCode = $true
            $targetCodium = $true
            $targetCodeOss = $true
        }

        $vsExtSrc = $vscodeDir

        # Official VS Code
        if ($targetCode) {
            $codeExtDir = Join-Path $env:USERPROFILE ".vscode\extensions\rook-lang-0.7.1"
            Write-Host ">>> Installing Rook extension for VS Code to $codeExtDir..."
            New-Item -ItemType Directory -Force -Path (Split-Path $codeExtDir) | Out-Null
            if (Test-Path $codeExtDir) { Remove-Item -Recurse -Force $codeExtDir }
            Copy-Item -Recurse -Force $vsExtSrc $codeExtDir
            if (Get-Command code -ErrorAction SilentlyContinue) {
                try { & code --install-extension "$vsExtSrc" 2>$null } catch {}
            }
            Write-Host "VS Code extension installed!"
        }

        # VSCodium
        if ($targetCodium) {
            Write-Host ">>> Installing Rook extension for VSCodium..."
            $codiumDir = Join-Path $env:USERPROFILE ".vscode-oss\extensions\rook-lang-0.7.1"
            New-Item -ItemType Directory -Force -Path (Split-Path $codiumDir) | Out-Null
            if (Test-Path $codiumDir) { Remove-Item -Recurse -Force $codiumDir }
            Copy-Item -Recurse -Force $vsExtSrc $codiumDir

            $vscodiumDir = Join-Path $env:USERPROFILE ".vscodium\extensions\rook-lang-0.7.1"
            if (Test-Path (Join-Path $env:USERPROFILE ".vscodium") -or (Test-Path (Join-Path $env:APPDATA "VSCodium"))) {
                New-Item -ItemType Directory -Force -Path (Split-Path $vscodiumDir) | Out-Null
                if (Test-Path $vscodiumDir) { Remove-Item -Recurse -Force $vscodiumDir }
                Copy-Item -Recurse -Force $vsExtSrc $vscodiumDir
            }
            if (Get-Command codium -ErrorAction SilentlyContinue) {
                try { & codium --install-extension "$vsExtSrc" 2>$null } catch {}
            }
            Write-Host "VSCodium extension installed!"
        }

        # Code - OSS
        if ($targetCodeOss) {
            $ossDir = Join-Path $env:USERPROFILE ".vscode-oss\extensions\rook-lang-0.7.1"
            if (-not $targetCodium) {
                Write-Host ">>> Installing Rook extension for Code - OSS to $ossDir..."
                New-Item -ItemType Directory -Force -Path (Split-Path $ossDir) | Out-Null
                if (Test-Path $ossDir) { Remove-Item -Recurse -Force $ossDir }
                Copy-Item -Recurse -Force $vsExtSrc $ossDir
            }
            if (Get-Command code-oss -ErrorAction SilentlyContinue) {
                try { & code-oss --install-extension "$vsExtSrc" 2>$null } catch {}
            }
            Write-Host "Code - OSS extension installed!"
        }
    }
}

Write-Host "=========================================="
Write-Host "    Rook installation completed!          "
Write-Host "=========================================="
