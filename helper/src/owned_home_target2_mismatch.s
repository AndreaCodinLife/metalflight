.text
.p2align 12
.globl owned_home_target2
owned_home_target2:
# exact captured prologue of game+0x5DD280 (mismatch variant: third store differs)
.byte 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x49,0x89,0x6b,0x18,0x49,0x89,0x4b,0x10
.byte 0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x50,0x01,0x00,0x00
movl $42,%eax
cmpq %rbx,384(%rsp)
jne home2_bad
cmpq %rcx,392(%rsp)
jne home2_bad
cmpq %rbp,400(%rsp)
je home2_return
home2_bad:
movl $-1,%eax
home2_return:
addq $0x150,%rsp
popq %r15
popq %r14
popq %r13
popq %rdi
popq %rsi
retq

.p2align 8
.globl home2_stack_call
home2_stack_call:
pushq %rbx
pushq %rbp
pushq %rsi
pushq %rdi
pushq %r12
pushq %r13
pushq %r14
pushq %r15
movq %rsp,%r12
movq %rdx,%r11
movq %rcx,%rsp
movq %rsp,%r10
movq %r8,%rdx
movq %r8,%rcx
movl $0x3333,%ebx
movl $0x5555,%ebp
movl $0x6666,%esi
movl $0x7777,%edi
movl $0xdddd,%r13d
movl $0xeeee,%r14d
movl $0xffff,%r15d
pushq %r10
pushq %rdx
subq $32,%rsp
callq *%r11
addq $32,%rsp
popq %rdx
popq %r10
cmpl $42,%eax
jne home2_call_bad
cmpq %rsp,%r10
jne home2_call_bad
cmpq $0x3333,%rbx
jne home2_call_bad
cmpq $0x5555,%rbp
jne home2_call_bad
cmpq $0x6666,%rsi
jne home2_call_bad
cmpq $0x7777,%rdi
jne home2_call_bad
cmpq $0xdddd,%r13
jne home2_call_bad
cmpq $0xeeee,%r14
jne home2_call_bad
cmpq $0xffff,%r15
je home2_call_return
home2_call_bad:
movl $-2,%eax
home2_call_return:
movq %r12,%rsp
popq %r15
popq %r14
popq %r13
popq %r12
popq %rdi
popq %rsi
popq %rbp
popq %rbx
retq
