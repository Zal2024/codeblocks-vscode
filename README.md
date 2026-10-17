# Code::Blocks with vs code feel
Do you even have a university that dont let you use vs code because vs code has a bunch of AI features? you've come to the right repo! This transforms **Code::Blocks** into a modern **VS Code Dark+** environment with **JetBrains Mono** typography, **50% opacity** selection/brace highlights, **`Ctrl + I/J/K/L`** cursor navigation, **live inline GCC/G++ LSP diagnostics (squiggles + Error Lens)**, and **single-file `F5` debugging** without needing a `.cbp` project.

---

## Features

1. **VS Code Dark+ Theme & Typography
   - Dark `#1E1E1E` editor background, dark gutters/line numbers/folding margins, and authentic VS Code Dark+ C/C++ & Python syntax palette.
   - **JetBrains Mono 11** default font (also bundles **Cascadia Code** and **Fira Code**).
   - **Cursive Italic Comments** (`JetBrains Mono Italic` in `#6A9955` olive green) while code stays crisp upright.
   - **1.35x Line-Height Breathing Room** (`+2px` extra ascent & descent via Scintilla) + **LCD Subpixel Anti-Aliasing**.
   - **50% Opacity Highlights** for text selection (`#233C58`), matching braces (`#2A485C`), and word occurrences.

2. **`Ctrl + I / J / K / L` Cursor Navigation**
   - `Ctrl + I` / `Ctrl + K` / `Ctrl + J` / `Ctrl + L` — Move **Up / Down / Left / Right**
   - `Ctrl + Shift + I / J / K / L` — **Select** Up / Left / Down / Right
   - `Ctrl + Alt + J / L` — Jump **Word Left / Word Right**
   - `Ctrl + Alt + Shift + J / L` — **Select Word** Left / Right

3. **Live VS Code LSP Diagnostics & Error Lens**
   - Background `gcc`/`g++ -fsyntax-only -Wall -Wextra -Wformat=2` checker runs automatically as you type.
   - Draws red/yellow **squiggly underlines** (`INDIC_SQUIGGLE`) and inline **Error Lens** boxed messages directly underneath lines with errors or warnings.
   - Press **`Ctrl + Shift + M`** to toggle inline Error Lens annotations on/off.

4. **Single-File Debugging (`F5` & `Ctrl + F5`) — No Project Required**
   - **`F5`** — Compiles the active `.c`/`.cpp` file with `-g3 -O0` and launches **GDB Visual Split-Screen (`gdb -tui`)**.
   - **`Ctrl + F5`** — Compiles and runs the active file with **AddressSanitizer + UndefinedBehaviorSanitizer** (`-fsanitize=address,undefined -g3`) to catch segfaults, out-of-bounds accesses, and memory bugs with exact line numbers.

---

## Quick Install

### Linux (Ubuntu / Debian / Mint / Arch / Fedora)

```bash
git clone https://github.com/Zal2024/codeblocks-vscode.git
cd codeblocks-vscode
./install.sh
```

### Windows (PowerShell)

```powershell
git clone https://github.com/Zal2024/codeblocks-vscode.git
cd codeblocks-vscode
powershell -ExecutionPolicy Bypass -File .\windows\install-windows.ps1
```
*(For `Ctrl + I/J/K/L` navigation on Windows, run [`windows/vscode_ijkl.ahk`](windows/vscode_ijkl.ahk) with [AutoHotkey v2](https://www.autohotkey.com/).)*
