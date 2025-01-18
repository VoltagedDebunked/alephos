[BITS 64]
global syscall_entry

extern syscall_handler

section .text

syscall_entry:
    ; Save user stack
    swapgs                      ; Switch to kernel GS
    mov [gs:16], rsp           ; Save user RSP
    mov rsp, [gs:8]           ; Load kernel RSP

    ; Create stack frame
    push qword [gs:16]        ; Save user RSP
    push r11                  ; Save user RFLAGS
    push rcx                  ; Save user RIP

    ; Save registers
    push rax                  ; System call number
    push rdi                  ; Arg 1
    push rsi                  ; Arg 2
    push rdx                  ; Arg 3
    push r10                  ; Arg 4 (rcx is used by syscall instruction)
    push r8                   ; Arg 5
    push r9                   ; Arg 6

    ; Align stack for function call
    sub rsp, 8

    ; Call C handler
    mov rcx, r10             ; Restore arg4 from r10
    call syscall_handler

    ; Restore stack alignment
    add rsp, 8

    ; Restore registers
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax

    ; Restore user context
    pop rcx                  ; Restore RIP
    pop r11                  ; Restore RFLAGS
    mov rsp, [gs:16]        ; Restore user RSP
    swapgs                  ; Restore user GS

    ; Return to user mode
    sysretq