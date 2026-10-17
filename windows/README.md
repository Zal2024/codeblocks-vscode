# Windows Setup for Code::Blocks VS Code Edition

> [!WARNING]
> **Windows Version is Experimental & Unstable:**
> Code::Blocks was designed primarily around Linux/GTK conventions. On Windows, differences in wxWidgets styling, Win32 GDI font handling, and background parser thread locks make the Windows build prone to quirks and instability.
> 
> We strongly recommend using the **Linux** version whenever possible for the most stable and authentic VS Code experience.

---

## Installation

Open PowerShell in this directory and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\install-windows.ps1
```

## Included Components

- **`windows/config/default.conf`**: Pre-configured VS Code Dark+ color palette, JetBrains Mono font settings, and Scintilla styles.
- **`windows/scripts/startup.script`**: Code::Blocks startup squirrel script to initialize a warmup workspace and resolve the "Parser is still parsing files" deadlock on Windows.
- **`windows/bin/cb_single_file_debug.ps1`**: Single-file GDB visual split-screen debugger (F5) and AddressSanitizer/UBSan runner (Ctrl+F5).
- **`windows/bin/cb_cph_judge.py`**: Competitive Programming Helper (CPH) judge for testcase execution.
- **`windows/vscode_ijkl.ahk`**: AutoHotkey v2 script for smooth `Ctrl + I/J/K/L` cursor navigation and keyboard shortcuts.
