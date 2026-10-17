# PowerShell Installer for Code::Blocks VS Code Edition on Windows
$ErrorActionPreference = "Stop"
$RepoDir = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoDir "fonts"))) {
    $RepoDir = $PSScriptRoot
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Installing Code::Blocks VS Code Edition (Windows)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 0. Ensure Code::Blocks is closed before writing config
$cbProc = Get-Process -Name codeblocks -ErrorAction SilentlyContinue
if ($cbProc) {
    Write-Host "[!] Closing running Code::Blocks instance..." -ForegroundColor Yellow
    Stop-Process -Name codeblocks -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

# 1. Install JetBrains Mono, Cascadia Code, and Fira Code fonts to User Fonts
$UserFonts = Join-Path $env:LOCALAPPDATA "Microsoft\Windows\Fonts"
New-Item -ItemType Directory -Force -Path $UserFonts | Out-Null
Get-ChildItem -Path (Join-Path $RepoDir "fonts\*.ttf") | ForEach-Object {
    $dest = Join-Path $UserFonts $_.Name
    try {
        Copy-Item -Path $_.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
        New-ItemProperty -Path "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts" -Name "$($_.BaseName) (TrueType)" -Value $dest -PropertyType String -Force | Out-Null
    } catch {}
}
Write-Host "[1/4] Installed JetBrains Mono, Cascadia Code & Fira Code fonts." -ForegroundColor Green

# 2. Compile / install libvscode_helper.dll runtime engine
$CbUserDataDir = Join-Path $env:APPDATA "CodeBlocks"
$CbBinDir = Join-Path $CbUserDataDir "bin"
New-Item -ItemType Directory -Force -Path $CbBinDir | Out-Null

$Gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
$HelperDllSrc = Join-Path $RepoDir "windows\src\vscode_helper.c"
$HelperDllDest = Join-Path $CbBinDir "libvscode_helper.dll"

if (Test-Path (Join-Path $RepoDir "windows\bin\libvscode_helper.dll")) {
    Copy-Item -Path (Join-Path $RepoDir "windows\bin\libvscode_helper.dll") -Destination $HelperDllDest -Force
    Write-Host "[2/4] Installed libvscode_helper.dll runtime engine." -ForegroundColor Green
} elseif ($Gcc -and (Test-Path $HelperDllSrc)) {
    Write-Host "[2/4] Compiling libvscode_helper.dll..." -ForegroundColor Yellow
    & gcc -shared -fPIC -O2 "$HelperDllSrc" -o "$HelperDllDest"
    Write-Host "[2/4] Compiled & installed libvscode_helper.dll." -ForegroundColor Green
} else {
    Write-Host "[2/4] Note: GCC not found in PATH; skipping DLL build." -ForegroundColor Yellow
}

# 3. Install Single-File Debugger & CPH Judge scripts
Copy-Item -Path (Join-Path $RepoDir "windows\bin\cb_single_file_debug.ps1") -Destination (Join-Path $CbBinDir "cb_single_file_debug.ps1") -Force
Copy-Item -Path (Join-Path $RepoDir "windows\bin\cb_cph_judge.py") -Destination (Join-Path $CbBinDir "cb_cph_judge.py") -Force
Write-Host "[3/4] Installed single-file GDB/ASan debugger & CPH Judge." -ForegroundColor Green

# 4. Install Code::Blocks default.conf to %APPDATA%\CodeBlocks
$ConfDest = Join-Path $CbUserDataDir "default.conf"
if (Test-Path $ConfDest) {
    Copy-Item -Path $ConfDest -Destination "$ConfDest.backup" -Force
}

$WindowsConf = Join-Path $RepoDir "windows\config\default.conf"
if (-not (Test-Path $WindowsConf)) {
    $WindowsConf = Join-Path $RepoDir "config\default.conf"
}

$confText = Get-Content $WindowsConf -Raw
# Wire tools commands directly to installed user bin
$debugPs1Esc = (Join-Path $CbBinDir "cb_single_file_debug.ps1")
$cphPyEsc = (Join-Path $CbBinDir "cb_cph_judge.py")

$confText = [regex]::Replace($confText, '(?<=<tool0>[\s\S]*?<COMMAND>[\s\S]*?<!\[CDATA\[)[\s\S]*?(?=\]\]>)', "powershell.exe -ExecutionPolicy Bypass -File `"$debugPs1Esc`"")
$confText = [regex]::Replace($confText, '(?<=<tool1>[\s\S]*?<COMMAND>[\s\S]*?<!\[CDATA\[)[\s\S]*?(?=\]\]>)', "powershell.exe -ExecutionPolicy Bypass -File `"$debugPs1Esc`" -asan")
$confText = [regex]::Replace($confText, '(?<=<tool2>[\s\S]*?<COMMAND>[\s\S]*?<!\[CDATA\[)[\s\S]*?(?=\]\]>)', "python.exe `"$cphPyEsc`"")

[System.IO.File]::WriteAllText($ConfDest, $confText, [System.Text.Encoding]::UTF8)
Write-Host "[4/4] Installed Windows VS Code Dark+ theme, font info, & tools." -ForegroundColor Green

# 5. Create Desktop launcher shortcut for Code::Blocks VS Code Edition
$CbExe = "C:\Program Files\CodeBlocks\codeblocks.exe"
if (-not (Test-Path $CbExe)) {
    $foundCb = Get-ChildItem "C:\Program Files\CodeBlocks\codeblocks.exe", "C:\Program Files (x86)\CodeBlocks\codeblocks.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($foundCb) { $CbExe = $foundCb.FullName }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Installation completed successfully!" -ForegroundColor Green
Write-Host "  - F5: Single-File GDB Visual Debugger" -ForegroundColor Cyan
Write-Host "  - Ctrl+F5: AddressSanitizer + UndefinedBehaviorSanitizer" -ForegroundColor Cyan
Write-Host "  - F6 / Ctrl+Alt+B: CPH Judge" -ForegroundColor Cyan
Write-Host "  - Run windows\vscode_ijkl.ahk for Ctrl+I/J/K/L cursor navigation." -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Cyan
