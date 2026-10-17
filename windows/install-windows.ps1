# PowerShell Installer for Code::Blocks VS Code Edition on Windows
$ErrorActionPreference = "Stop"
$RepoDir = Split-Path -Parent $PSScriptRoot

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Installing Code::Blocks VS Code Edition (Windows)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Install JetBrains Mono, Cascadia Code, and Fira Code fonts to User Fonts
$UserFonts = Join-Path $env:LOCALAPPDATA "Microsoft\Windows\Fonts"
New-Item -ItemType Directory -Force -Path $UserFonts | Out-Null
Get-ChildItem -Path (Join-Path $RepoDir "fonts\*.ttf") | ForEach-Object {
    $dest = Join-Path $UserFonts $_.Name
    Copy-Item -Path $_.FullName -Destination $dest -Force
    New-ItemProperty -Path "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts" -Name "$($_.BaseName) (TrueType)" -Value $dest -PropertyType String -Force | Out-Null
}
Write-Host "[1/2] Installed JetBrains Mono, Cascadia Code & Fira Code fonts." -ForegroundColor Green

# 2. Install Code::Blocks default.conf to %APPDATA%\CodeBlocks
$CbConfigDir = Join-Path $env:APPDATA "CodeBlocks"
New-Item -ItemType Directory -Force -Path $CbConfigDir | Out-Null
$ConfDest = Join-Path $CbConfigDir "default.conf"
if (Test-Path $ConfDest) {
    Copy-Item -Path $ConfDest -Destination "$ConfDest.backup" -Force
}
Copy-Item -Path (Join-Path $RepoDir "config\default.conf") -Destination $ConfDest -Force
Write-Host "[2/2] Installed VS Code Dark+ theme & config to $ConfDest" -ForegroundColor Green

Write-Host ""
Write-Host "Done! Optional: Run windows\vscode_ijkl.ahk with AutoHotkey v2 for Ctrl+I/J/K/L navigation on Windows." -ForegroundColor Yellow
