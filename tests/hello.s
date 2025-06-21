/* Простейшая программа на ARM64 */
.section .text
.global _start

_start:
    // Системный вызов write(1, message, 13)
    mov x0, #1          /* файловый дескриптор (stdout) */
    adr x1, message     /* адрес строки */
    mov x2, #13         /* длина строки */
    mov x8, #64         /* номер системного вызова write */
    svc #0              /* вызов системы */

    // Системный вызов exit(0)
    mov x0, #0          /* код возврата */
    mov x8, #93         /* номер системного вызова exit */
    svc #0              /* вызов системы */

message:
    .ascii "Hello, World!\n"
