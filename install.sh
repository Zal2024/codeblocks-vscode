#!/usr/bin/env bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================================="
echo "  Installing Code::Blocks VS Code Edition (Linux)"
echo "=========================================================="

# 1. Ensure Code::Blocks is closed before writing default.conf
if pgrep -x codeblocks >/dev/null 2>&1; then
    echo "[!] Closing running Code::Blocks instance so config can be updated..."
    pkill -x codeblocks || true
    sleep 1
fi

# 2. Install Developer Fonts (JetBrains Mono, Cascadia Code, Fira Code)
echo "[1/5] Installing developer fonts (JetBrains Mono, Cascadia Code, Fira Code)..."
mkdir -p "$HOME/.local/share/fonts/vscode_fonts"
cp -f "$REPO_DIR"/fonts/*.ttf "$HOME/.local/share/fonts/vscode_fonts/"
if command -v fc-cache >/dev/null 2>&1; then
    fc-cache -f "$HOME/.local/share/fonts/vscode_fonts"
fi

# 3. Install Code::Blocks default.conf (adapting user home directory paths)
echo "[2/5] Installing VS Code Dark+ theme & editor config..."
mkdir -p "$HOME/.config/codeblocks"
if [ -f "$HOME/.config/codeblocks/default.conf" ]; then
    cp -f "$HOME/.config/codeblocks/default.conf" "$HOME/.config/codeblocks/default.conf.backup"
fi
sed "s|/home/fazal|$HOME|g" "$REPO_DIR/config/default.conf" > "$HOME/.config/codeblocks/default.conf"

# 4. Compile & install libvscode_helper.so + plugin proxy loader
echo "[3/5] Compiling libvscode_helper.so..."
mkdir -p "$HOME/.local/share/codeblocks/plugins"
gcc -shared -fPIC -O2 "$REPO_DIR/src/vscode_helper.c" -o "$HOME/.local/share/codeblocks/libvscode_helper.so" -ldl -lpthread

# 5. Install single-file debugger, CPH Judge, & launcher scripts
echo "[4/5] Installing single-file GDB/ASan debugger & CPH Judge scripts..."
mkdir -p "$HOME/.local/bin"
cp -f "$REPO_DIR/bin/cb_single_file_debug.sh" "$HOME/.local/bin/cb_single_file_debug.sh"
cp -f "$REPO_DIR/bin/cb_cph_judge.py" "$HOME/.local/bin/cb_cph_judge.py"
cp -f "$REPO_DIR/bin/codeblocks" "$HOME/.local/bin/codeblocks"
chmod +x "$HOME/.local/bin/cb_single_file_debug.sh" "$HOME/.local/bin/cb_cph_judge.py" "$HOME/.local/bin/codeblocks"

# Ignore auto-generated workspace file in git
mkdir -p "$HOME/.config/git"
grep -qxF ".vscode_workspace.cbp" "$HOME/.config/git/ignore" 2>/dev/null || echo ".vscode_workspace.cbp" >> "$HOME/.config/git/ignore"
grep -qxF ".vscode_workspace.layout" "$HOME/.config/git/ignore" 2>/dev/null || echo ".vscode_workspace.layout" >> "$HOME/.config/git/ignore"

# 6. Configure desktop launcher to preload libvscode_helper.so automatically
echo "[5/5] Configuring desktop shortcut..."
mkdir -p "$HOME/.local/share/applications"
if [ -f /usr/share/applications/codeblocks.desktop ]; then
    sed "s|^Exec=.*|Exec=env LD_PRELOAD=$HOME/.local/share/codeblocks/libvscode_helper.so /usr/bin/codeblocks %F|g" \
        /usr/share/applications/codeblocks.desktop > "$HOME/.local/share/applications/codeblocks.desktop"
fi

echo ""
echo "=========================================================="
echo "  Done! Launch Code::Blocks to enjoy the VS Code setup."
echo "=========================================================="
