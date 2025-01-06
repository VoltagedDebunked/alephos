[BITS 64]
section .text

; External C handler
extern exception_handler_common  ; Changed from exception_handler to exception_handler_common

; Export the ISR stub table
global isr_stub_table

; Create ISR stub table
align 16
isr_stub_table:
    %assign i 0
    %rep 256
        dq isr_stub_%+i
    %assign i i+1
    %endrep

; Macro for ISRs that don't push an error code
%macro ISR_NOERROR 1
align 16
isr_stub_%1:
    push    qword 0        ; Push dummy error code
    push    qword %1       ; Push interrupt number
    jmp     isr_common     ; Jump to common handler
%endmacro

; Macro for ISRs that do push an error code
%macro ISR_ERROR 1
align 16
isr_stub_%1:
    push    qword %1       ; Push interrupt number (error code already pushed by CPU)
    jmp     isr_common     ; Jump to common handler
%endmacro

; Common interrupt handling code
align 16
isr_common:
    ; Save all general purpose registers
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ; Save SIMD state
    mov     rax, cr0
    mov     rdx, rax
    and     rax, 0xFFFB   ; Clear TS bit
    mov     cr0, rax      ; Disable #NM
    
    sub     rsp, 512
    fxsave  [rsp]         ; Save FPU/SSE state

    ; Call C handler
    mov     rcx, rsp      ; Pass pointer to interrupt frame
    call    exception_handler_common  ; Changed from exception_handler to exception_handler_common

    ; Restore SIMD state
    fxrstor [rsp]
    add     rsp, 512
    
    mov     cr0, rdx      ; Restore original CR0

    ; Restore general purpose registers
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    ; Remove error code and interrupt number
    add     rsp, 16
    
    ; Return from interrupt
    iretq

; Generate stubs for all interrupts
; CPU Exceptions (some push error codes, some don't)
ISR_NOERROR    0   ; Divide by Zero
ISR_NOERROR    1   ; Debug
ISR_NOERROR    2   ; Non-maskable Interrupt
ISR_NOERROR    3   ; Breakpoint
ISR_NOERROR    4   ; Overflow
ISR_NOERROR    5   ; Bound Range Exceeded
ISR_NOERROR    6   ; Invalid Opcode
ISR_NOERROR    7   ; Device Not Available
ISR_ERROR      8   ; Double Fault
ISR_NOERROR    9   ; Coprocessor Segment Overrun (reserved)
ISR_ERROR      10  ; Invalid TSS
ISR_ERROR      11  ; Segment Not Present
ISR_ERROR      12  ; Stack-Segment Fault
ISR_ERROR      13  ; General Protection Fault
ISR_ERROR      14  ; Page Fault
ISR_NOERROR    15  ; Reserved
ISR_NOERROR    16  ; x87 Floating-Point Exception
ISR_ERROR      17  ; Alignment Check
ISR_NOERROR    18  ; Machine Check
ISR_NOERROR    19  ; SIMD Floating-Point Exception
ISR_NOERROR    20  ; Virtualization Exception
ISR_ERROR      21  ; Control Protection Exception
ISR_NOERROR    22  ; Reserved
ISR_NOERROR    23  ; Reserved
ISR_NOERROR    24  ; Reserved
ISR_NOERROR    25  ; Reserved
ISR_NOERROR    26  ; Reserved
ISR_NOERROR    27  ; Reserved
ISR_NOERROR    28  ; Reserved
ISR_NOERROR    29  ; Reserved
ISR_ERROR      30  ; Security Exception
ISR_NOERROR    31  ; Reserved

; Generate handlers for remaining interrupts (32-255)
%assign i 32
%rep    224
    ISR_NOERROR i
%assign i i+1
%endrep