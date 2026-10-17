# Requires AutoHotkey v2.0
; VS Code Keybindings & Navigation Extension for Code::Blocks on Windows
#HotIf WinActive("ahk_exe codeblocks.exe")

; Ctrl + I/J/K/L Cursor Navigation
^i::Send "{Up}"
^k::Send "{Down}"
^j::Send "{Left}"
^l::Send "{Right}"

^+i::Send "+{Up}"
^+k::Send "+{Down}"
^+j::Send "+{Left}"
^+l::Send "+{Right}"

^!j::Send "^{Left}"
^!l::Send "^{Right}"

^!+j::Send "^+{Left}"
^!+l::Send "^+{Right}"

; F5 = Single-File Visual GDB Debugger
F5:: {
    debugScript := A_AppData "\CodeBlocks\bin\cb_single_file_debug.ps1"
    if !FileExist(debugScript)
        debugScript := A_ScriptDir "\bin\cb_single_file_debug.ps1"
    Run 'powershell.exe -ExecutionPolicy Bypass -NoProfile -WindowStyle Normal -File "' debugScript '"'
}

; Ctrl + F5 = AddressSanitizer + UndefinedBehaviorSanitizer Runner
^F5:: {
    debugScript := A_AppData "\CodeBlocks\bin\cb_single_file_debug.ps1"
    if !FileExist(debugScript)
        debugScript := A_ScriptDir "\bin\cb_single_file_debug.ps1"
    Run 'powershell.exe -ExecutionPolicy Bypass -NoProfile -WindowStyle Normal -File "' debugScript '" -asan'
}

; F6 or Ctrl + Alt + B = CPH Judge: Run Testcases
F6:: {
    cphScript := A_AppData "\CodeBlocks\bin\cb_cph_judge.py"
    if !FileExist(cphScript)
        cphScript := A_ScriptDir "\bin\cb_cph_judge.py"
    Run 'python.exe "' cphScript '" --run'
}

^!b:: {
    cphScript := A_AppData "\CodeBlocks\bin\cb_cph_judge.py"
    if !FileExist(cphScript)
        cphScript := A_ScriptDir "\bin\cb_cph_judge.py"
    Run 'python.exe "' cphScript '" --run'
}

#HotIf
