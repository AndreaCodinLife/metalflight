.p2align 8
.globl catalog_stack_call
catalog_stack_call:
pushq %rbx
pushq %r12
movq %rsp,%r12
movabsq $0x123456789abcdef,%rbx
movq %rcx,%rsp
callq *%rdx
movq %r12,%rsp
movabsq $0x123456789abcdef,%r11
cmpq %r11,%rbx
je 1f
movl $-99,%eax
1:
popq %r12
popq %rbx
retq
