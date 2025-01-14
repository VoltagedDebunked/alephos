section .text
bits 16

global ap_trampoline
global ap_trampoline_end

; This code is copied to 0x8000 and executed by APs in real mode
ap_trampoline:
    cli                     ; Disable interrupts
    cld                     ; Clear direction flag

    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable protected mode
    lgdt [gdt_ptr - ap_trampoline + 0x8000]
    mov eax, cr0
    or al, 1               ; Set PE bit
    mov cr0, eax

    ; Jump to protected mode
    jmp 0x08:protected_mode - ap_trampoline + 0x8000

bits 32
protected_mode:
    ; Set up protected mode segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load startup data address
    mov esi, 0x6000        ; AP startup data page

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5        ; Set PAE bit
    mov cr4, eax

    ; Load page table
    mov eax, [esi + 8]    ; Load CR3 value from startup data
    mov cr3, eax

    ; Enable long mode
    mov ecx, 0xC0000080   ; EFER MSR
    rdmsr
    or eax, 1 << 8        ; Set LME bit
    wrmsr

    ; Enable paging and protection
    mov eax, cr0
    or eax, 1 << 31 | 1   ; Set PG and PE bits
    mov cr0, eax

    ; Jump to long mode
    jmp 0x08:long_mode - ap_trampoline + 0x8000

bits 64
long_mode:
    ; Set up long mode segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Get our startup data
    mov rsi, 0x6000        ; AP startup data page

    ; Set up stack
    mov rsp, [rsi + 4]     ; Load stack pointer

    ; Load APIC ID
    mov eax, [rsi]

    ; Release startup lock
    mov rdi, rsi
    add rdi, 16            ; Offset to startup_lock
    mov dword [rdi], 0     ; Clear lock

    ; Call AP entry point
    mov rax, [rsi + 12]    ; Load entry point
    call rax

    ; Should never return
    cli
    hlt
.hang:
    jmp .hang

; GDT for AP startup
align 16
gdt:
    dq 0                   ; Null descriptor
    dq 0x00209A0000000000 ; Code segment (64-bit)
    dq 0x0000920000000000 ; Data segment
gdt_ptr:
    dw $ - gdt - 1        ; GDT limit
    dd gdt - ap_trampoline + 0x8000 ; GDT base

ap_trampoline_end: