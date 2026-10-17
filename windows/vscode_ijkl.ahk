#Requires AutoHotkey v2.0
; VS Code IJKL Navigation Extension for Code::Blocks on Windows
#HotIf WinActive("ahk_exe codeblocks.exe")

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

#HotIf
