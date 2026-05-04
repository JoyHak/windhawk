#Requires autohotkey v2
#include <Container/Container>

; Count all messages
inputFile  := 'dialog-render-messages.log'
outputFile := 'dialog-render-messages-count.exec'
win32      := 'win32.ini'


; Count frequencies
counts := Map()
counts.caseSense := false
counts.capacity  := 100

loop read, inputFile {    
    if counts.Has(A_LoopReadLine) {
        counts[A_LoopReadLine] += 1
    } else {
        counts[A_LoopReadLine] := 1
    }
}


; Save message names
names  := Map()
names.caseSense := false
names.capacity  := counts.capacity

loop read, win32 {
    ; Skip sections
    if ('WM_' != SubStr(A_LoopReadLine, 1, 3))
        continue

    ; Map each message number to it's name
    pair := StrSplit(A_LoopReadLine, '=')
    names.Set(pair[2], pair[1])
}

; Put all message-count pairs into container 
; to sort it by frequency
messages := Container.CbNumber((m) => m.count)
messages.capacity := counts.capacity

for msg, count in counts {
    messages.Push({
        msg: names.Get(msg, msg), 
        count: count
    })
}

messages := messages.QuickSort()


; Dump container to the file
lines := ''
for m in messages {
    lines .= m.msg ' ' m.count '`n'
}

f := FileOpen(outputFile, 'w')
f.Write(lines)
f.Close()

Run(outputFile)