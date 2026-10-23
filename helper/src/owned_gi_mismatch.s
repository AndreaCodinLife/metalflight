# mismatch: rsi/rdi swapped in the run, epilogue follows the swapped order
.text
.p2align 12
# game+0x4107cb0 shape: seven pushes (rbp,rbx,rsi,rdi,r12,r14,r15) then lea rbp,[rsp-27h]
# and sub rsp,0xD0. No home stores before the run.
.globl owned_gi_function
owned_gi_function:
.globl owned_gi_target
owned_gi_target:
.byte 0x55,0x53,0x57,0x56,0x41,0x54,0x41,0x56,0x41,0x57
.byte 0x48,0x8d,0x6c,0x24,0xd9
.byte 0x48,0x81,0xec,0xd0,0x00,0x00,0x00
movq $42,%rax
addq $0xd0,%rsp
popq %r15
popq %r14
popq %r12
popq %rsi
popq %rdi
popq %rbx
popq %rbp
retq
