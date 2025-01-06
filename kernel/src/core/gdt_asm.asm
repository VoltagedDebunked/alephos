[BITS 64]
section .text

global gdt_flush

; gdt_flush(uint64_t gdt_ptr)
; RDI contains the pointer to the GDT descriptor
gdt_flush:
    ; Load GDT
    lgdt    [rdi]
    
    ; Reload CS through far return
    push    0x08                ; Kernel code segment selector
    lea     rax, [rel .reload_cs] ; Get address of reload_cs
    push    rax
    retfq                       ; Far return to reload CS
.reload_cs:
    ; Reload data segment registers
    mov     ax, 0x10           ; Kernel data segment selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    
    ; Load TSS
    mov     ax, 0x2B           ; TSS segment selector (5 * 8 + 3)
    ltr     ax
    
    ret