.3ds

.open "code.bin", "build/patched_code.bin", 0x100000

;                  (r0)        (r1)
; int strcmp(char* str1, char* str2)
strcmp equ 0x1011E8

; Relevant registers:
; - r1: input_hostname (we are hooking the copy from r9 to r1)
; - r2: host_size (we are hooking before a memcpy)
; - r5: host_size
; - r9: input_hostname
gethostbyname_hook_addr equ 0x12C660
gethostbyname_hook_end equ 0x12C664

; Relevant registers:
; - r2: node (we are hooking the copy from r2 to r0 with gt condition)
; - r3: node_size
getaddrinfo_hook_addr equ 0x12C3E8
getaddrinfo_hook_end equ 0x12C3EC

; This is where the code cave starts, we have space up to 0x132000
start_replacements_addr equ 0x131088

.org gethostbyname_hook_addr
	b gethostbyname_hook

.org getaddrinfo_hook_addr
	bgt getaddrinfo_hook ; we replicate the original condition in this instruction

.org start_replacements_addr
	gethostbyname_hook: ; 0x131088
		bl    handle_gethostbyname_replacements
		b     gethostbyname_hook_end            ; go back to the original function

	getaddrinfo_hook: ; 0x131090
		bl    handle_getaddrinfo_replacements
		b     getaddrinfo_hook_end            ; go back to the original function

	handle_gethostbyname_replacements:
		stmdb sp!, {r0, r3, r4, r6, r7, r8, r9, r10, r11, r12, lr} ; save the current state just in case
		mov   r0, r9                                               ; move original hostname to r0 for replacements
		bl    handle_replacements
		cmp   r1, #0                                               ; check if the pointer has been modified (a size is returned)
		movgt r2, r1                                               ; if we did, modify the hostname size on r2 (the memcpy used later)
		movgt r5, r1                                               ; and on the original register r5
		mov   r1, r0                                               ; move the returned hostname to r1 (original behavior)
		ldmia sp!, {r0, r3, r4, r6, r7, r8, r9, r10, r11, r12, pc} ; load the original state back and return

	; Note: We do not need to modify the size here because getaddrinfo handles it using strlen
	handle_getaddrinfo_replacements:
		stmdb sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, lr} ; save the current state just in case
		mov   r0, r2                                                       ; move original hostname to r0 (original behavior)
		bl    handle_replacements
		ldmia sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, pc} ; load the original state back and return

	; Homestead: both NNCS domains resolve to the custom server address in
	; hs_server (filled in by the Homestead app). Returns the pointer and its
	; length, or the original hostname and 0 when it is not an NNCS domain.
	; (r0, r1)                          (r0)
	; char* handle_replacements(char* original)
	handle_replacements:
		stmdb sp!, {r4, r10, r12, lr}  ; save the current state
		mov   r10, r0                  ; keep the original hostname
		ldr   r1, =nncs1_orig_name
		bl    strcmp
		cmp   r0, #0
		beq   handle_replacements_match
		mov   r0, r10
		ldr   r1, =nncs2_orig_name
		bl    strcmp
		cmp   r0, #0
		beq   handle_replacements_match
		mov   r0, r10                  ; not NNCS: original hostname, size 0
		mov   r1, #0
		b     handle_replacements_end

	handle_replacements_match:
		ldr   r0, =hs_server
		mov   r1, #0
	handle_replacements_strlen:
		ldrb  r4, [r0, r1]
		cmp   r4, #0
		addne r1, r1, #1
		bne   handle_replacements_strlen

	handle_replacements_end:
		ldmia sp!, {r4, r10, r12, pc}  ; load the original state back and return

; strings
	.pool

	nncs1_orig_name:
		.asciiz "nncs1.app.nintendowifi.net"

	nncs2_orig_name:
		.asciiz "nncs2.app.nintendowifi.net"

	.align 4
	hs_server: ; 64-byte slot, NUL-padded, rewritten by the Homestead app
		.asciiz "homestead.invalid"
		.fill 64 - 18, 0

.close
