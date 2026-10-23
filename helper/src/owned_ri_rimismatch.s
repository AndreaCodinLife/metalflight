.text
.p2align 12
# ntdll RtlImageDirectoryEntryToData shape: four pushes (rbp,rdi,rsi,rbx), sub rsp,0x28, then its
# first body bytes (xor eax,eax; mov rbx,rcx; test cl,1). The mismatch variant swaps rdi/rsi.
.globl owned_ri_function
owned_ri_function:
.globl owned_ri_target
owned_ri_target:
.byte 0x55,0x56,0x57,0x53
.byte 0x48,0x83,0xec,0x28
.byte 0x31,0xc0,0x48,0x89,0xcb,0xf6,0xc1,0x01
movq $42,%rax
addq $0x28,%rsp
popq %rbx
popq %rdi
popq %rsi
popq %rbp
retq
