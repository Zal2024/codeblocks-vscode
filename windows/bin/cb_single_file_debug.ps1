# PowerShell Single-File Debugger & AddressSanitizer Runner for Code::Blocks on Windows
param (
    [switch]$asan,
    [string]$TargetFile = ""
)

$Host.UI.RawUI.WindowTitle = "VS Code Single-File Debugger"

if (-not $TargetFile -or -not (Test-Path $TargetFile)) {
    # Check %TEMP%\cb_active_file.txt written by vscode_helper
    $tempFile = Join-Path $env:TEMP "cb_active_file.txt"
    if (Test-Path $tempFile) {
        $p = (Get-Content $tempFile -Raw).Trim()
        if (Test-Path $p) {
            $TargetFile = $p
        }
    }
}

if (-not $TargetFile -or -not (Test-Path $TargetFile)) {
    # Fallback: scan user's Documents or desktop for most recently modified source file
    $found = Get-ChildItem -Path "$env:USERPROFILE\Documents", "$env:USERPROFILE\Desktop" -Recurse -Depth 3 -Include *.c, *.cpp, *.cc -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($found) {
        $TargetFile = $found.FullName
    }
}

if (-not $TargetFile -or -not (Test-Path $TargetFile)) {
    Write-Host "[!] No active C/C++ source file detected in Code::Blocks." -ForegroundColor Red
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

$Dir = Split-Path -Parent $TargetFile
$Base = Split-Path -Leaf $TargetFile
$Ext = [System.IO.Path]::GetExtension($TargetFile).ToLower()
$Stem = [System.IO.Path]::GetFileNameWithoutExtension($TargetFile)
$OutBin = Join-Path $env:TEMP "cb_debug_$Stem.exe"

$CcCmd = if ($Ext -in @(".cpp", ".cc", ".cxx")) { "g++" } else { "gcc" }

Set-Location $Dir

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  VS Code-Style Single-File Debugger: $Base" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""

if ($asan) {
    Write-Host "[Compiling with AddressSanitizer + UndefinedBehaviorSanitizer...]" -ForegroundColor Yellow
    & $CcCmd -g3 -O0 -Wall -Wextra -fsanitize=address,undefined "$TargetFile" -o "$OutBin"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[Compilation Failed! Fix the errors above.]" -ForegroundColor Red
        Write-Host "Press any key to exit..."
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        exit 1
    }

    Write-Host "`n[Running $Base with Memory & Crash Sanitizer...]`n" -ForegroundColor Green
    $env:ASAN_OPTIONS = "color=always:halt_on_error=1:abort_on_error=1:print_stacktrace=1"
    $env:UBSAN_OPTIONS = "color=always:print_stacktrace=1"
    
    & "$OutBin"
    $exitVal = $LASTEXITCODE
    Write-Host "`n----------------------------------------------------------" -ForegroundColor Cyan
    Write-Host "Process exited with code: $exitVal" -ForegroundColor Cyan
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
} else {
    Write-Host "[Compiling $Base with Debug Symbols (-g3 -O0)...]" -ForegroundColor Yellow
    & $CcCmd -g3 -O0 -Wall -Wextra "$TargetFile" -o "$OutBin"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[Compilation Failed! Fix the errors above.]" -ForegroundColor Red
        Write-Host "Press any key to exit..."
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        exit 1
    }

    Write-Host "`n[Launching GDB Visual TUI (n=next, s=step, c=continue, p var=print, q=quit)]" -ForegroundColor Green
    Start-Sleep -Milliseconds 400
    & gdb -tui -q `
        -ex "set pagination off" `
        -ex "set print pretty on" `
        -ex "set print array on" `
        -ex "layout src" `
        -ex "focus cmd" `
        -ex "break main" `
        -ex "run" `
        "$OutBin"
}
