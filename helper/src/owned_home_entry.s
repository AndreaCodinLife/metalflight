.text
.p2align 12
.globl owned_home_target
owned_home_target:
movq %rdx,0x10(%rsp)
movq %r8,0x18(%rsp)
movq %r9,0x20(%rsp)
pushq %rbx
pushq %rbp
pushq %rsi
pushq %rdi
subq $0x38,%rsp
pushfq
popq %r10
movq %r10,home_flags(%rip)
movl $42,%eax
cmpq %rdx,0x68(%rsp)
jne home_bad
cmpq %r8,0x70(%rsp)
jne home_bad
cmpq %r9,0x78(%rsp)
je home_return
home_bad:
movl $-1,%eax
home_return:
addq $0x38,%rsp
popq %rdi
popq %rsi
popq %rbp
popq %rbx
retq

.p2align 8
.globl home_stack_call
home_stack_call:
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
movq %rsp,%r13
movq %r8,%rdx
movq %rdx,%r14
leaq 1(%rdx),%r8
leaq 2(%rdx),%r9
movl $0x3333,%ebx
movl $0x5555,%ebp
movl $0x6666,%esi
movl $0x7777,%edi
callq *%r11
cmpl $42,%eax
jne home_call_bad
cmpq %rsp,%r13
jne home_call_bad
cmpq %rdx,%r14
jne home_call_bad
leaq 1(%r14),%r10
cmpq %r8,%r10
jne home_call_bad
leaq 2(%r14),%r10
cmpq %r9,%r10
jne home_call_bad
cmpq $0x3333,%rbx
jne home_call_bad
cmpq $0x5555,%rbp
jne home_call_bad
cmpq $0x6666,%rsi
jne home_call_bad
cmpq $0x7777,%rdi
je home_call_return
home_call_bad:
movl $-2,%eax
home_call_return:
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
