#!/usr/bin/env bash
# Builds the VS Code toolbar plugin: libvscode_toolbar.so + vscode_toolbar.zip (its manifest).
# Usage: tools/build_toolbar_plugin.sh OUT_DIR
#
# A Code::Blocks plugin must be compiled against the SDK headers of the exact Code::Blocks
# build that loads it. When the matching codeblocks-dev / wx dev packages are not installed,
# they are downloaded with apt-get download and unpacked into a temp dir (no root needed).
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:?usage: $0 OUT_DIR}"
mkdir -p "$OUT_DIR"

pkg_version() { dpkg-query -W -f='${Version}' "$1" 2>/dev/null || true; }
pkg_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'; }

command -v g++ >/dev/null 2>&1 || { echo "g++ not found"; exit 1; }
command -v dpkg-query >/dev/null 2>&1 || { echo "not a Debian/Ubuntu system"; exit 1; }

CB_VER="$(pkg_version codeblocks)"
[ -n "$CB_VER" ] || { echo "the codeblocks package is not installed"; exit 1; }

# The wx runtime package was renamed for the 64-bit time_t transition (…-1t64)
WX_RUNTIME="$(dpkg-query -W -f='${Package}\n' 'libwxgtk3.2-*' 2>/dev/null | while read -r p; do
    pkg_installed "$p" && { echo "$p"; break; }; done)"
WX_VER="$(pkg_version "$WX_RUNTIME")"
[ -n "$WX_VER" ] || { echo "wxWidgets 3.2 (GTK) runtime not found"; exit 1; }

ROOT=""
if [ "$(pkg_version codeblocks-dev)" = "$CB_VER" ] && pkg_installed codeblocks-dev \
   && pkg_installed libwxgtk3.2-dev && pkg_installed libtinyxml-dev; then
    ROOT=""   # system headers match the running Code::Blocks
else
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    echo "    downloading Code::Blocks $CB_VER SDK headers (no install)..."
    (cd "$TMP" && apt-get download -q "codeblocks-dev=$CB_VER" "libwxgtk3.2-dev=$WX_VER" \
        "wx3.2-headers=$WX_VER" libtinyxml-dev >/dev/null)
    for deb in "$TMP"/*.deb; do dpkg-deb -x "$deb" "$TMP/root"; done
    ROOT="$TMP/root"
fi

MULTIARCH="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
CB_INC="$ROOT/usr/include/codeblocks"
[ -f "$CB_INC/sdk.h" ] || { echo "Code::Blocks SDK headers not found"; exit 1; }

find_lib() {
    for d in /usr/lib/"$MULTIARCH" /usr/lib /lib/"$MULTIARCH" /lib /usr/lib64; do
        [ -e "$d/$1" ] && { echo "$d/$1"; return; }
    done
    echo "missing library $1" >&2; return 1
}
LIBS="$(find_lib libcodeblocks.so.0) $(find_lib libwx_gtk3u_aui-3.2.so.0) \
$(find_lib libwx_gtk3u_core-3.2.so.0) $(find_lib libwx_baseu-3.2.so.0)"

# shellcheck disable=SC2086
g++ -shared -fPIC -O2 -std=c++14 -w \
    -DBUILDING_PLUGIN -D__WXGTK__ -DWXUSINGDLL -D_FILE_OFFSET_BITS=64 \
    -I"$REPO_DIR/src" -I"$ROOT/usr/include" \
    -I"$CB_INC" -I"$CB_INC/scripting/include" -I"$CB_INC/scripting/bindings" -I"$CB_INC/wxscintilla/include" \
    -I"$ROOT/usr/lib/$MULTIARCH/wx/include/gtk3-unicode-3.2" -I"$ROOT/usr/include/wx-3.2" \
    "$REPO_DIR/src/vscode_toolbar.cpp" -o "$OUT_DIR/libvscode_toolbar.so" $LIBS -ldl

# Code::Blocks only loads a plugin that has a <name>.zip manifest next to the plugins dir
python3 - "$REPO_DIR/plugin/manifest.xml" "$OUT_DIR/vscode_toolbar.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], 'manifest.xml')
PY
