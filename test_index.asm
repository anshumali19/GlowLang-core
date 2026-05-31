bits 64
mov r11, 0x0000FFFFFFFFFFFF
and rax, r11
and r10, r11
mov r11, [r10 + 40]
mov rax, [r11 + rax*8]
