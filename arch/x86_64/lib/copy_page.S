/* Written 2003 by Andi Kleen, based on a kernel by Evandro Menezes */
	
/* Don't use streaming store because it's better when the target
   ends up in cache. */
	    
/* Could vary the prefetch distance based on SMP/UP */

	.globl copy_page
	.p2align 4
copy_page:
#ifdef CONFIG_PREFETCH
	prefetch (%rsi) 
	prefetch 1*64(%rsi)
	prefetch 2*64(%rsi)
	prefetch 3*64(%rsi)
	prefetch 4*64(%rsi)
	prefetchw (%rdi) 
	prefetchw 1*64(%rdi) 
	prefetchw 2*64(%rdi) 
	prefetchw 3*64(%rdi)
	prefetchw 4*64(%rdi)
#else
	prefetchnta (%rsi)
	prefetchnta 1*64(%rsi)
	prefetchnta 2*64(%rsi)
	prefetchnta 3*64(%rsi)
	prefetchnta 4*64(%rsi)
#endif
	subq	$3*8,%rsp
	movq	%rbx,(%rsp)
	movq	%r12,1*8(%rsp)
	movq	%r13,2*8(%rsp)
			
	movl	$(4096/64)-5,%ecx
	.p2align 4
.Loop64:	
  	dec     %rcx

	movq        (%rsi), %rax
	movq      8 (%rsi), %rbx
	movq     16 (%rsi), %rdx
	movq     24 (%rsi), %r8
	movq     32 (%rsi), %r9
	movq     40 (%rsi), %r10
	movq     48 (%rsi), %r11
	movq     56 (%rsi), %r12
#ifdef CONFIG_PREFETCH
	prefetch 5*64(%rsi)
#else
	prefetchnta 5*64(%rsi)
#endif
	movq     %rax,    (%rdi)
	movq     %rbx,  8 (%rdi)
	movq     %rdx, 16 (%rdi)
	movq     %r8,  24 (%rdi)
	movq     %r9,  32 (%rdi)
	movq     %r10, 40 (%rdi)
	movq     %r11, 48 (%rdi)
	movq     %r12, 56 (%rdi)
#ifdef CONFIG_PREFETCH
	prefetchw 5*64(%rdi)
#endif
	leaq    64 (%rsi), %rsi
	leaq    64 (%rdi), %rdi

	jnz     .Loop64

	movl	$5,%ecx
	.p2align 4
.Loop2:	
	decl   %ecx

	movq        (%rsi), %rax
	movq      8 (%rsi), %rbx
	movq     16 (%rsi), %rdx
	movq     24 (%rsi), %r8
	movq     32 (%rsi), %r9
	movq     40 (%rsi), %r10
	movq     48 (%rsi), %r11
	movq     56 (%rsi), %r12

	movq     %rax,    (%rdi)
	movq     %rbx,  8 (%rdi)
	movq     %rdx, 16 (%rdi)
	movq     %r8,  24 (%rdi)
	movq     %r9,  32 (%rdi)
	movq     %r10, 40 (%rdi)
	movq     %r11, 48 (%rdi)
	movq     %r12, 56 (%rdi)
	
	leaq	64(%rdi),%rdi			
	leaq	64(%rsi),%rsi			
	
	jnz	.Loop2		
	
	movq	(%rsp),%rbx
	movq	1*8(%rsp),%r12
	movq	2*8(%rsp),%r13
	addq	$3*8,%rsp
	ret
