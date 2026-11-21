@ Homestead: entry wrapper for the http sysmodule's URL setup function.
@
@ The original function (0x113820, firmware 11.17) is
@     Result f(Context* ctx, const char* url)
@ It allocates strlen(url)+1 bytes and copies the URL, and Nimbus rewrites
@ the URL in place after that allocation, so Nimbus can only shrink hosts.
@ This wrapper runs first: for the Dev-environment account it rewrites the
@ URL into a stack buffer and calls the original body with that buffer, so
@ the allocation is sized for the new URL. The original function keeps no
@ pointer to the caller's URL once it returns (it only uses its own copy).
@
@ Addresses below are filled in by build_patches.py with --defsym.

    .arm
    .section .text.hs_entry, "ax"
    .global hs_entry

    .equ HS_BUF_SIZE, 0x180

hs_entry:
    push    {r0-r4, lr}             @ 24 bytes, keeps 8-byte alignment
    ldr     r4, =GET_NASC_ENVIRONMENT
    blx     r4                      @ Nimbus helper: NASC environment + 1
    cmp     r0, #3                  @ Dev environment = Homestead account
    beq     1f
    pop     {r0-r4, lr}
    b       hs_original             @ not ours: run the original untouched

1:  sub     sp, sp, #HS_BUF_SIZE
    mov     r0, sp                  @ out
    ldr     r1, [sp, #HS_BUF_SIZE + 4]  @ original url (saved r1)
    mov     r2, #HS_BUF_SIZE
    blx     hs_rewrite              @ Thumb code
    mov     r1, r0                  @ url to use
    ldr     r0, [sp, #HS_BUF_SIZE + 0]  @ ctx (saved r0)
    ldr     r2, [sp, #HS_BUF_SIZE + 8]
    ldr     r3, [sp, #HS_BUF_SIZE + 12]
    bl      hs_original             @ returns the Result in r0
    add     sp, sp, #HS_BUF_SIZE + 16
    pop     {r4, pc}

hs_original:
    push    {r3-r11, lr}            @ the instruction we replaced
    ldr     pc, =ORIGINAL_CONTINUE  @ continue at the next original instruction

    .ltorg
