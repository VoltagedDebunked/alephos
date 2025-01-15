[BITS 64]
section .text

global context_switch
context_switch:
    ; Save old context if exists (rdi contains old_state)
    cmp rdi, 0
    je .load_new

    ; Save all registers
    mov [rdi + 0x00], rax
    mov [rdi + 0x08], rbx
    mov [rdi + 0x10], rcx
    mov [rdi + 0x18], rdx
    mov [rdi + 0x20], rdi
    mov [rdi + 0x28], rsi
    mov [rdi + 0x30], rbp
    mov [rdi + 0x38], r8
    mov [rdi + 0x40], r9
    mov [rdi + 0x48], r10
    mov [rdi + 0x50], r11
    mov [rdi + 0x58], r12
    mov [rdi + 0x60], r13
    mov [rdi + 0x68], r14
    mov [rdi + 0x70], r15

    ; Save instruction pointer, stack pointer, and flags
    mov rax, [rsp]      ; Get return address
    mov [rdi + 0x78], rax  ; Save as RIP
    mov [rdi + 0x80], cs
    pushfq
    pop rax
    mov [rdi + 0x88], rax
    mov [rdi + 0x90], rsp
    mov [rdi + 0x98], ss

.load_new:
    ; Load new context (rsi contains new_state)
    mov rsp, [rsi + 0x90]  ; Restore stack pointer
    mov rax, [rsi + 0x88]  ; Restore flags
    push rax
    popfq
    mov rax, [rsi + 0x78]  ; Get new instruction pointer
    push rax               ; Push for ret

    ; Restore all registers
    mov rax, [rsi + 0x00]
    mov rbx, [rsi + 0x08]
    mov rcx, [rsi + 0x10]
    mov rdx, [rsi + 0x18]
    mov rdi, [rsi + 0x20]
    mov rsi, [rsi + 0x28]
    mov rbp, [rsi + 0x30]
    mov r8,  [rsi + 0x38]
    mov r9,  [rsi + 0x40]
    mov r10, [rsi + 0x48]
    mov r11, [rsi + 0x50]
    mov r12, [rsi + 0x58]
    mov r13, [rsi + 0x60]
    mov r14, [rsi + 0x68]
    mov r15, [rsi + 0x70]

    ; Jump to new process
    ret