# PowerShell Installer for Code::Blocks VS Code Edition on Windows
$ErrorActionPreference = "Stop"
$RepoDir = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoDir "fonts"))) {
    $RepoDir = $PSScriptRoot
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Installing Code::Blocks VS Code Edition (Windows)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 0. Ensure Code::Blocks is closed before writing config & plugin DLL
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

# 2. Compile / install libvscode_helper.dll runtime engine into Code::Blocks user plugins
$CbUserDataDir = Join-Path $env:APPDATA "CodeBlocks"
$CbBinDir = Join-Path $CbUserDataDir "bin"
$CbShareDir = Join-Path $CbUserDataDir "share\codeblocks"
$CbPluginsDir = Join-Path $CbShareDir "plugins"
New-Item -ItemType Directory -Force -Path $CbBinDir | Out-Null
New-Item -ItemType Directory -Force -Path $CbPluginsDir | Out-Null

$Gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
$HelperDllSrc = Join-Path $RepoDir "windows\src\vscode_helper.c"
$HelperDllBin = Join-Path $CbBinDir "libvscode_helper.dll"
$HelperPluginDll = Join-Path $CbPluginsDir "vscode_helper.dll"
$HelperPluginZip = Join-Path $CbShareDir "vscode_helper.zip"

if ($Gcc -and (Test-Path $HelperDllSrc)) {
    & gcc -shared -O2 "$HelperDllSrc" -o "$HelperDllBin" -luser32 -lgdi32
    Copy-Item -Path $HelperDllBin -Destination $HelperPluginDll -Force
    Write-Host "[2/4] Compiled & installed vscode_helper.dll runtime plugin." -ForegroundColor Green
} elseif (Test-Path (Join-Path $RepoDir "windows\bin\libvscode_helper.dll")) {
    Copy-Item -Path (Join-Path $RepoDir "windows\bin\libvscode_helper.dll") -Destination $HelperDllBin -Force
    Copy-Item -Path $HelperDllBin -Destination $HelperPluginDll -Force
    Write-Host "[2/4] Installed pre-built vscode_helper.dll runtime plugin." -ForegroundColor Green
} else {
    Write-Host "[2/4] Note: GCC not found in PATH; skipping DLL build." -ForegroundColor Yellow
}

# Create/install vscode_helper.zip manifest so Code::Blocks PluginManager loads vscode_helper.dll automatically
if (Test-Path (Join-Path $RepoDir "windows\bin\vscode_helper.zip")) {
    Copy-Item -Path (Join-Path $RepoDir "windows\bin\vscode_helper.zip") -Destination $HelperPluginZip -Force
} else {
    $manifestXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<CodeBlocks_plugin_manifest_file>
    <SdkVersion major="1" minor="11" release="12" />
    <Plugin name="vscode_helper">
        <Value title="VS Code Runtime Engine" />
        <Value version="1.0" />
        <Value description="VS Code Dark+ runtime engine for Code::Blocks" />
        <Value author="Zal2024" />
        <Value authorEmail="" />
        <Value authorWebsite="" />
        <Value thanksTo="" />
        <Value license="GPL-3.0" />
    </Plugin>
</CodeBlocks_plugin_manifest_file>
"@
    $tmpMan = Join-Path $env:TEMP "manifest.xml"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tmpMan, $manifestXml, $utf8NoBom)
    if (Test-Path $HelperPluginZip) { Remove-Item $HelperPluginZip -Force }
    Compress-Archive -Path $tmpMan -DestinationPath $HelperPluginZip -Force
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
# Wire tools commands directly to installed user bin (literal replace, avoiding regex bleed)
$debugPs1Esc = (Join-Path $CbBinDir "cb_single_file_debug.ps1")
$cphPyEsc = (Join-Path $CbBinDir "cb_cph_judge.py")
$confText = $confText.Replace('%APPDATA%\CodeBlocks\bin\cb_single_file_debug.ps1', $debugPs1Esc)
$confText = $confText.Replace('%APPDATA%\CodeBlocks\bin\cb_cph_judge.py', $cphPyEsc)

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($ConfDest, $confText, $utf8NoBom)

# 5. Install Warmup Workspace & startup.script (fixes "The Parser is still parsing files.")
$CbScriptsDir = Join-Path $CbShareDir "scripts"
$CbWorkspaceDir = Join-Path $CbUserDataDir "workspace"
New-Item -ItemType Directory -Force -Path $CbScriptsDir | Out-Null
New-Item -ItemType Directory -Force -Path $CbWorkspaceDir | Out-Null

$StartupScriptSrc = Join-Path $RepoDir "windows\scripts\startup.script"
if (Test-Path $StartupScriptSrc) {
    Copy-Item -Path $StartupScriptSrc -Destination (Join-Path $CbScriptsDir "startup.script") -Force
}

$WarmupWorkspaceSrc = Join-Path $RepoDir "windows\workspace"
if (Test-Path $WarmupWorkspaceSrc) {
    Copy-Item -Path (Join-Path $WarmupWorkspaceSrc "*") -Destination $CbWorkspaceDir -Recurse -Force
}

# 6. Configure cbKeyBinder accelerators if present
$KeyBinderConf = Join-Path $CbUserDataDir "default.cbKeyBinder20.conf"
if (Test-Path $KeyBinderConf) {
    try {
        $kbText = Get-Content $KeyBinderConf -Raw
        $kbJson = $kbText | ConvertFrom-Json
        foreach ($item in $kbJson.menus) {
            if ($item.resourceID -eq "943") { $item.accelerator = "F5" }
            if ($item.resourceID -eq "944") { $item.accelerator = "Ctrl-F5" }
            if ($item.resourceID -eq "945") { $item.accelerator = "F6" }
        }
        $kbJson | ConvertTo-Json -Depth 10 | Set-Content $KeyBinderConf -Encoding UTF8
    } catch {}
}
Write-Host "[4/4] Installed Windows VS Code Dark+ theme, font info, warmup workspace & tools." -ForegroundColor Green

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Installation completed successfully!" -ForegroundColor Green
Write-Host "  - F5: Single-File GDB Visual Debugger" -ForegroundColor Cyan
Write-Host "  - Ctrl+F5: AddressSanitizer + UndefinedBehaviorSanitizer" -ForegroundColor Cyan
Write-Host "  - F6 / Ctrl+Alt+B: CPH Judge" -ForegroundColor Cyan
Write-Host "  - Instant C/C++ keywords & autocomplete in Untitled tabs" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
