; kernel/isr_stubs.asm
; Interrupt service routine stubs.
; One stub per vector; each pushes a fake error code (if the CPU
; doesn't push one) and the vector number, then jumps to isr_common.

extern isr_dispatch
global isr_stub_table

section .text

%macro ISR_NOERR 1
isr%1:
    push qword 0            ; fake error code
    push qword %1           ; vector number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
    ; CPU already pushed the error code
    push qword %1           ; vector number
    jmp isr_common
%endmacro

; ------------------------------------------------------------
; CPU exceptions 0..31
; ------------------------------------------------------------

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; ------------------------------------------------------------
; IRQs and everything else: 32..255
; ------------------------------------------------------------

%assign v 32
%rep 224
    ISR_NOERR v
    %assign v v+1
%endrep

; ------------------------------------------------------------
; Common entry / exit
; ------------------------------------------------------------

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; MS x64 ABI:
    ; first argument = RCX
    ; reserve 32-byte shadow space for called C function
    mov rcx, rsp
    sub rsp, 32
    call isr_dispatch
    add rsp, 32

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

; ------------------------------------------------------------
; Stub address table used by idt_install_handlers()
; ------------------------------------------------------------

section .data
global isr_stub_table
isr_stub_table:
%assign v 0
%rep 256
    dq isr %+ v
    %assign v v+1
%endrep