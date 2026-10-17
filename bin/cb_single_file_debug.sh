#!/usr/bin/env bash
# VS Code-style Single-File Debugger & AddressSanitizer Runner for Code::Blocks
MODE="gdb"
TARGET_FILE=""

for arg in "$@"; do
    if [ "$arg" = "--asan" ]; then
        MODE="asan"
    elif [ -f "$arg" ]; then
        TARGET_FILE="$arg"
    fi
done

if [ -z "$TARGET_FILE" ] || [ ! -f "$TARGET_FILE" ]; then
    CB_PID=$(pgrep -x codeblocks | head -n 1)
    if [ -n "$CB_PID" ]; then
        CWD=$(readlink -f "/proc/$CB_PID/cwd" 2>/dev/null)
        if [ -d "$CWD" ]; then
            TARGET_FILE=$(find "$CWD" -maxdepth 1 \( -name "*.c" -o -name "*.cpp" \) -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)
        fi
    fi
fi

if [ -z "$TARGET_FILE" ] || [ ! -f "$TARGET_FILE" ]; then
    TARGET_FILE=$(find "$HOME/Documents" -maxdepth 3 \( -name "*.c" -o -name "*.cpp" \) -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)
fi

DIR=$(dirname "$TARGET_FILE")
BASE=$(basename "$TARGET_FILE")
EXT="${BASE##*.}"
NAME="${BASE%.*}"
OUT_BIN="/tmp/cb_debug_${NAME}"

if [ "$EXT" = "cpp" ] || [ "$EXT" = "cc" ] || [ "$EXT" = "cxx" ]; then
    CC_CMD="g++"
else
    CC_CMD="gcc"
fi

RUNNER="/tmp/cb_run_debug.sh"
cat << SCRIPT > "$RUNNER"
#!/usr/bin/env bash
cd "$DIR" || exit 1
echo -e "\033[1;34m==========================================================\033[0m"
echo -e "\033[1;36m  VS Code-Style Single-File Debugger: $BASE\033[0m"
echo -e "\033[1;34m==========================================================\033[0m"

if [ "$MODE" = "asan" ]; then
    echo -e "\033[1;33m[Compiling with AddressSanitizer + UndefinedBehaviorSanitizer...]\033[0m"
    $CC_CMD -g3 -O0 -Wall -Wextra -fsanitize=address,undefined "$TARGET_FILE" -o "$OUT_BIN" -lm
    if [ \$? -ne 0 ]; then
        echo -e "\n\033[1;31m[Compilation Failed! Fix the errors above.]\033[0m"
        read -rp "Press Enter to close..."
        exit 1
    fi
    echo -e "\033[1;32m[Running $BASE with Memory & Crash Sanitizer...]\033[0m\n"
    export ASAN_OPTIONS="color=always:halt_on_error=1:abort_on_error=1:print_stacktrace=1"
    export UBSAN_OPTIONS="color=always:print_stacktrace=1"
    "$OUT_BIN"
    EXIT_CODE=\$?
    echo -e "\n\033[1;34m----------------------------------------------------------\033[0m"
    echo -e "Process exited with code: \$EXIT_CODE"
    read -rp "Press Enter to close..."
else
    echo -e "\033[1;33m[Compiling $BASE with Debug Symbols (-g3 -O0)...]\033[0m"
    $CC_CMD -g3 -O0 -Wall -Wextra "$TARGET_FILE" -o "$OUT_BIN" -lm
    if [ \$? -ne 0 ]; then
        echo -e "\n\033[1;31m[Compilation Failed! Fix the errors above.]\033[0m"
        read -rp "Press Enter to close..."
        exit 1
    fi
    echo -e "\033[1;32m[Launching GDB Visual TUI (n=next, s=step, c=continue, p var=print, q=quit)]\033[0m"
    sleep 0.4
    gdb -tui -q \
        -ex "set pagination off" \
        -ex "set print pretty on" \
        -ex "set print array on" \
        -ex "layout src" \
        -ex "focus cmd" \
        -ex "break main" \
        -ex "run" \
        "$OUT_BIN"
fi
SCRIPT
chmod +x "$RUNNER"

if command -v mate-terminal >/dev/null 2>&1; then
    mate-terminal --title="VS Code Debugger - $BASE" --geometry=125x36 -e "$RUNNER"
elif command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal --title="VS Code Debugger - $BASE" -- "$RUNNER"
elif command -v xfce4-terminal >/dev/null 2>&1; then
    xfce4-terminal --title="VS Code Debugger - $BASE" -e "$RUNNER"
elif command -v konsole >/dev/null 2>&1; then
    konsole -e "$RUNNER"
elif command -v x-terminal-emulator >/dev/null 2>&1; then
    x-terminal-emulator -e "$RUNNER"
else
    xterm -e "$RUNNER"
fi
