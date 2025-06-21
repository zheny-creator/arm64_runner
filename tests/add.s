.section .data
buf: .asciz "0\n"

.section .text
.global _start
_start:
    mov x0, #1      // x0 = 1
    mov x1, #2      // x1 = 2
    add x2, x0, x1  // x2 = x0 + x1

    // Преобразуем x2 (0..9) в ASCII-цифру
    add x3, x2, #'0'    // x3 = x2 + '0'
    ldr x4, =buf
    strb w3, [x4]       // buf[0] = ASCII-цифра

    // write(1, buf, 2)
    mov x0, #1          // fd = 1 (stdout)
    ldr x1, =buf        // buf
    mov x2, #2          // len = 2 (цифра + \n)
    mov x8, #64         // syscall write
    svc #0

    // exit(0)
    mov x0, #0
    mov x8, #93
    svc #0 