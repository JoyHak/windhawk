#Requires autohotkey v2
#include <Container/Container>

inputFile  := 'window-render.log'
outputFile := 'window-render.exec'
win32      := 'win32.ini'

; Save message names
names  := Map()
names.caseSense := false
names.capacity  := 100

loop read, win32 {
    ; Skip sections
    if ('WM_' != SubStr(A_LoopReadLine, 1, 3))
        continue
    
    ; Map each message number to it's name
    pair := StrSplit(A_LoopReadLine, '=')
    names.Set(pair[2], pair[1])
}


; Count frequencies
counts := Map()
counts.caseSense := false
counts.capacity  := 100

loop read, inputFile {    
    if !RegExMatch(A_LoopReadLine, '[^{]+{msg: ([^}]+)', &match)
        continue
        
    msg := match[1]

    if counts.Has(msg) {
        counts[msg] += 1
    } else {
        counts[msg] := 1
    }
}

; Get readable names
messages := Map()
messages.caseSense := false
messages.capacity  := 100
lines := ''

loop read, inputFile {  
    if !RegExMatch(A_LoopReadLine, '[^{]+{msg: ([^}]+)', &match)
        continue
    
    msg := match[1]
    lines .= Format(
        '{} ({})`n', 
        names.Get(msg, msg), 
        counts.Get(msg)
    )
}

f := FileOpen(outputFile, 'w')
f.Write(lines)
f.Close()

Run(outputFile)