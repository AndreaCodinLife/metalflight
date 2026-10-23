.text
.p2align 12
# kernelbase function 0x3de20 shape: hotpatch pad, then the PUSH run (mismatch variant swaps rdi/rsi)
.globl owned_kb_function
owned_kb_function:
.byte 0x48,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00
.globl owned_kb_target
owned_kb_target:
.byte 0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x57,0x56,0x53
.byte 0x48,0x81,0xec,0x98,0x00,0x00,0x00
movl $42,%eax
addq $0x98,%rsp
popq %rbx
popq %rsi
popq %rdi
popq %rbp
popq %r12
popq %r13
popq %r14
popq %r15
retq

.p2align 8
.globl kb_stack_call
kb_stack_call:
pushq %rbx
pushq %rbp
pushq %rsi
pushq %rdi
pushq %r12
pushq %r13
pushq %r14
pushq %r15
movq %rsp,%r10
movq %rdx,%r11
movq %rcx,%rsp
pushq %r10
subq $40,%rsp
movq %rsp,%r10
movl $0x3333,%ebx
movl $0x5555,%ebp
movl $0x6666,%esi
movl $0x7777,%edi
movl $0xcccc,%r12d
movl $0xdddd,%r13d
movl $0xeeee,%r14d
movl $0xffff,%r15d
movq %r10,32(%rsp)
callq *%r11
movq 32(%rsp),%r10
cmpl $42,%eax
jne kb_call_bad
cmpq %rsp,%r10
jne kb_call_bad
cmpq $0x3333,%rbx
jne kb_call_bad
cmpq $0x5555,%rbp
jne kb_call_bad
cmpq $0x6666,%rsi
jne kb_call_bad
cmpq $0x7777,%rdi
jne kb_call_bad
cmpq $0xcccc,%r12
jne kb_call_bad
cmpq $0xdddd,%r13
jne kb_call_bad
cmpq $0xeeee,%r14
jne kb_call_bad
cmpq $0xffff,%r15
je kb_call_return
kb_call_bad:
movl $-3,%eax
kb_call_return:
addq $40,%rsp
popq %rsp
popq %r15
popq %r14
popq %r13
popq %r12
popq %rdi
popq %rsi
popq %rbp
popq %rbx
retq
