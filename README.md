# Code::Blocks with vs code feel
Do you have a university that dont let you use vs code because vs code has a bunch of AI features? you've come to the right repo! This transforms **Code::Blocks** into a modern **VS Code Dark+** environment with **JetBrains Mono** typography, **50% opacity** selection/brace highlights, **`Ctrl + I/J/K/L`** cursor navigation, **live inline GCC/G++ LSP diagnostics (squiggles + Error Lens)**, and **single-file `F5` debugging** without needing a `.cbp` project.

---

## Features

1. **VS Code Dark+ Theme & Typography**
   - Dark `#1E1E1E` editor background (including new unsaved `Untitled` / `Plain text` tabs!), dark gutters/line numbers/folding margins, and authentic VS Code Dark+ C/C++ & Python syntax palette.
   - **JetBrains Mono 11** default font (also bundles **Cascadia Code** and **Fira Code**).
   - **Cursive Italic Comments** (`JetBrains Mono Italic` in `#6A9955` olive green) while code stays crisp upright.
   - **1.35x Line-Height Breathing Room** (`+2px` extra ascent & descent via Scintilla) + **LCD Subpixel Anti-Aliasing**.
   - **50% Opacity Highlights** for text selection (`#233C58`), matching braces (`#2A485C`), and word occurrences.

2. **Auto-Workspace & Folder File Detection**
   - Opening or saving any file automatically sets its parent folder as the active **Workspace** in the left **Projects** sidebar and detects all other source/project files in that folder (including newly saved files), just like VS Code's Explorer.

3. **`Ctrl + I / J / K / L` Cursor Navigation**
   - `Ctrl + I` / `Ctrl + K` / `Ctrl + J` / `Ctrl + L` — Move **Up / Down / Left / Right**
   - `Ctrl + Shift + I / J / K / L` — **Select** Up / Left / Down / Right
   - `Ctrl + Alt + J / L` — Jump **Word Left / Word Right**
   - `Ctrl + Alt + Shift + J / L` — **Select Word** Left / Right

4. **Live VS Code LSP Diagnostics, Squiggles & Mouse Hover Tooltips**
   - Background `gcc`/`g++ -fsyntax-only -Wall -Wextra -Wformat=2` checker runs automatically as you type.
   - **Warnings** (such as `unused variable` or `uninitialized`) show only a subtle **yellow squiggly underline** underneath the code, and display the warning message in a dark popup tooltip **only when you hover your mouse over it**.
   - **Errors** draw red squiggly underlines and inline **Error Lens** boxes (press **`Ctrl + Shift + M`** to toggle inline boxes for warnings too).

5. **CPH Judge (Competitive Programming Helper) — `Ctrl + Alt + B` / `F6`**
   - Press **`Ctrl + Alt + B`** or **`F6`** (or **Tools → CPH Judge**) to open the **CPH Judge: Results** sidebar and run all testcases against your active file.
   - Reads and writes `.cph/*.prob` testcase files (100% compatible with VS Code's CPH extension), with `+ New Testcase`, per-testcase `▷` Run & `🗑` Delete, `Set ONLINE_JUDGE`, execution time in `ms`, and `N / M passed` badges.

6. **Single-File Debugging (`F5` & `Ctrl + F5`) — No Project Required**
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

> [!TIP]
> **Windows Keybindings & Single-File Shortcuts:**
> Run [`windows/vscode_ijkl.ahk`](windows/vscode_ijkl.ahk) with [AutoHotkey v2](https://www.autohotkey.com/) to enable:
> - `Ctrl + I / J / K / L` cursor navigation
> - `F5`: Single-File GDB Visual Debugger (`cb_single_file_debug.ps1`)
> - `Ctrl + F5`: AddressSanitizer + UndefinedBehaviorSanitizer Runner
> - `F6` or `Ctrl + Alt + B`: CPH Judge (`cb_cph_judge.py`)

