.section .data
out: .asciz "RES= \n"

.section .text
.global _start

_start:
    mov x0, #2
    add x0, x0, #3

    // Вывод результата (X0)
    mov x1, x0
    add x1, x1, #'0'
    ldr x2, =out
    strb w1, [x2, #4]   // out[4] = цифра
    mov x0, #1
    ldr x1, =out
    mov x2, #7           // длина "RES= N\n"
    mov x8, #64
    svc #0

    mov x8, #93         // syscall: exit
    mov x0, #0          // code 0
    svc #0 