/*
 *	setup.S		Copyright (C) 1991, 1992 Linus Torvalds
 *
 * setup.s is responsible for getting the system data from the BIOS,
 * and putting them into the appropriate places in system memory.
 * both setup.s and system has been loaded by the bootblock.
 *
 * This code asks the bios for memory/disk/other parameters, and
 * puts them in a "safe" place: 0x90000-0x901FF, ie where the
 * boot-block used to be. It is then up to the protected mode
 * system to read them from there before the area is overwritten
 * for buffer-blocks.
 *
 * Move PS/2 aux init code to psaux.c
 * (troyer@saifr00.cfsat.Honeywell.COM) 03Oct92
 *
 * some changes and additional features by Christoph Niemann,
 * March 1993/June 1994 (Christoph.Niemann@linux.org)
 *
 * add APM BIOS checking by Stephen Rothwell, May 1994
 * (sfr@canb.auug.org.au)
 *
 * High load stuff, initrd support and position independency
 * by Hans Lermen & Werner Almesberger, February 1996
 * <lermen@elserv.ffm.fgan.de>, <almesber@lrc.epfl.ch>
 *
 * Video handling moved to video.S by Martin Mares, March 1996
 * <mj@k332.feld.cvut.cz>
 *
 * Extended memory detection scheme retwiddled by orc@pell.chi.il.us (david
 * parsons) to avoid loadlin confusion, July 1997
 *
 * Transcribed from Intel (as86) -> AT&T (gas) by Chris Noe, May 1999.
 * <stiker@northlink.com>
 *
 * Fix to work around buggy BIOSes which dont use carry bit correctly
 * and/or report extended memory in CX/DX for e801h memory size detection 
 * call.  As a result the kernel got wrong figures.  The int15/e801h docs
 * from Ralf Brown interrupt list seem to indicate AX/BX should be used
 * anyway.  So to avoid breaking many machines (presumably there was a reason
 * to orginally use CX/DX instead of AX/BX), we do a kludge to see
 * if CX/DX have been changed in the e801 call and if so use AX/BX .
 * Michael Miller, April 2001 <michaelm@mjmm.org>
 *
 * Added long mode checking and SSE force. March 2003, Andi Kleen.		
 */

#include <linux/config.h>
#include <asm/segment.h>
#include <linux/version.h>
#include <linux/compile.h>
#include <asm/boot.h>
#include <asm/e820.h>

/* Signature words to ensure LILO loaded us right */
#define SIG1	0xAA55
#define SIG2	0x5A5A

INITSEG  = DEF_INITSEG		# 0x9000, we move boot here, out of the way
SYSSEG   = DEF_SYSSEG		# 0x1000, system loaded at 0x10000 (65536).
SETUPSEG = DEF_SETUPSEG		# 0x9020, this is the current segment
				# ... and the former contents of CS

DELTA_INITSEG = SETUPSEG - INITSEG	# 0x0020

.code16
.globl begtext, begdata, begbss, endtext, enddata, endbss

.text
begtext:
.data
begdata:
.bss
begbss:
.text

start:
	jmp	trampoline

# This is the setup header, and it must start at %cs:2 (old 0x9020:2)

		.ascii	"HdrS"		# header signature
		.word	0x0202		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG
		.word	kernel_version	# pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Don't
					# change it.

type_of_loader:	.byte	0		# = 0, old one (LILO, Loadlin,
					#      Bootlin, SYSLX, bootsect...)
					# See Documentation/i386/boot.txt for
					# assigned ids
	
# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
LOADED_HIGH	= 1			# If set, the kernel is loaded high
CAN_USE_HEAP	= 0x80			# If set, the loader also has set
					# heap_end_ptr to tell how much
					# space behind setup.S can be used for
					# heap purposes.
					# Only the loader knows what is free
#ifndef __BIG_KERNEL__
		.byte	0
#else
		.byte	LOADED_HIGH
#endif

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup 
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
#ifndef __BIG_KERNEL__
		.long	0x1000		#   0x1000 = default for zImage
#else
		.long	0x100000	# 0x100000 = default for big kernel
#endif

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.word  bootsect_helper, SETUPSEG

heap_end_ptr:	.word	modelist+1024	# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

pad1:		.word	0
cmd_line_ptr:	.long 0			# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

trampoline:	call	start_of_setup
		.space	1024
# End of setup header #####################################################

start_of_setup:
# Bootlin depends on this being done early
	movw	$0x01500, %ax
	movb	$0x81, %dl
	int	$0x13

#ifdef SAFE_RESET_DISK_CONTROLLER
# Reset the disk controller.
	movw	$0x0000, %ax
	movb	$0x80, %dl
	int	$0x13
#endif

# Set %ds = %cs, we know that SETUPSEG = %cs at this point
	movw	%cs, %ax		# aka SETUPSEG
	movw	%ax, %ds
# Check signature at end of setup
	cmpw	$SIG1, setup_sig1
	jne	bad_sig

	cmpw	$SIG2, setup_sig2
	jne	bad_sig

	jmp	good_sig1

# Routine to print asciiz string at ds:si
prtstr:
	lodsb
	andb	%al, %al
	jz	fin

	call	prtchr
	jmp	prtstr

fin:	ret

# Space printing
prtsp2:	call	prtspc		# Print double space
prtspc:	movb	$0x20, %al	# Print single space (note: fall-thru)

prtchr:	
	pushw	%ax
	pushw	%cx
	movw	$0007,%bx
	movw	$0x01, %cx
	movb	$0x0e, %ah
	int	$0x10
	popw	%cx
	popw	%ax
	ret

beep:	movb	$0x07, %al
	jmp	prtchr
	
no_sig_mess: .string	"No setup signature found ..."

good_sig1:
	jmp	good_sig

# We now have to find the rest of the setup code/data
bad_sig:
	movw	%cs, %ax			# SETUPSEG
	subw	$DELTA_INITSEG, %ax		# INITSEG
	movw	%ax, %ds
	xorb	%bh, %bh
	movb	(497), %bl			# get setup sect from bootsect
	subw	$4, %bx				# LILO loads 4 sectors of setup
	shlw	$8, %bx				# convert to words (1sect=2^8 words)
	movw	%bx, %cx
	shrw	$3, %bx				# convert to segment
	addw	$SYSSEG, %bx
	movw	%bx, %cs:start_sys_seg
# Move rest of setup code/data to here
	movw	$2048, %di			# four sectors loaded by LILO
	subw	%si, %si
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %es
	movw	$SYSSEG, %ax
	movw	%ax, %ds
	rep
	movsw
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
	cmpw	$SIG1, setup_sig1
	jne	no_sig

	cmpw	$SIG2, setup_sig2
	jne	no_sig

	jmp	good_sig

no_sig:
	lea	no_sig_mess, %si
	call	prtstr

no_sig_loop:
	jmp	no_sig_loop

good_sig:
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax 		# aka INITSEG
	movw	%ax, %ds
# Check if an old loader tries to load a big-kernel
	testb	$LOADED_HIGH, %cs:loadflags	# Do we have a big kernel?
	jz	loader_ok			# No, no danger for old loaders.

	cmpb	$0, %cs:type_of_loader 		# Do we have a loader that
						# can deal with us?
	jnz	loader_ok			# Yes, continue.

	pushw	%cs				# No, we have an old loader,
	popw	%ds				# die. 
	lea	loader_panic_mess, %si
	call	prtstr

	jmp	no_sig_loop

loader_panic_mess: .string "Wrong loader, giving up..."

loader_ok:
	/* check for long mode. */
	/* we have to do this before the VESA setup, otherwise the user
	   can't see the error message. */
	
	pushw	%ds
	movw	%cs,%ax
	movw	%ax,%ds
	
	/* minimum CPUID flags for x86-64 */
	/* see http://www.x86-64.org/lists/discuss/msg02971.html */		
#define SSE_MASK ((1<<25)|(1<<26))
#define REQUIRED_MASK1 ((1<<0)|(1<<3)|(1<<4)|(1<<5)|(1<<6)|(1<<8)|\
					   (1<<13)|(1<<15)|(1<<24))
#define REQUIRED_MASK2 (1<<29)

	pushfl				/* standard way to check for cpuid */
	popl	%eax
	movl	%eax,%ebx
	xorl	$0x200000,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	cmpl	%eax,%ebx
	jz	no_longmode		/* cpu has no cpuid */
	movl	$0x0,%eax
	cpuid
	cmpl	$0x1,%eax
	jb	no_longmode		/* no cpuid 1 */
	xor	%di,%di
	cmpl	$0x68747541,%ebx	/* AuthenticAMD */
	jnz	noamd
	cmpl	$0x69746e65,%edx
	jnz	noamd
	cmpl	$0x444d4163,%ecx
	jnz	noamd
	mov	$1,%di			/* cpu is from AMD */
noamd:
	movl    $0x1,%eax
	cpuid
	andl	$REQUIRED_MASK1,%edx
	xorl	$REQUIRED_MASK1,%edx
	jnz	no_longmode
	movl    $0x80000000,%eax
	cpuid
	cmpl    $0x80000001,%eax
	jb      no_longmode             /* no extended cpuid */
	movl    $0x80000001,%eax
	cpuid
	andl    $REQUIRED_MASK2,%edx
	xorl    $REQUIRED_MASK2,%edx
	jnz     no_longmode
sse_test:		
	movl	$1,%eax
	cpuid
	andl	$SSE_MASK,%edx
	cmpl	$SSE_MASK,%edx
	je	sse_ok
	test	%di,%di
	jz	no_longmode	/* only try to force SSE on AMD */ 
	movl	$0xc0010015,%ecx	/* HWCR */
	rdmsr
	btr	$15,%eax	/* enable SSE */
	wrmsr
	xor	%di,%di		/* don't loop */
	jmp	sse_test	/* try again */	
no_longmode:
	call	beep
	lea	long_mode_panic,%si
	call	prtstr
no_longmode_loop:		
	jmp	no_longmode_loop
long_mode_panic:
	.string "Your CPU does not support long mode. Use a 32bit distribution."
	.byte 0
	
sse_ok:
	popw	%ds

# tell BIOS we want to go to long mode
	movl  $0xec00,%eax	# declare target operating mode
	movl  $2,%ebx		# long mode
	int $0x15			
	
# Get memory size (extended mem, kB)

	xorl	%eax, %eax
	movl	%eax, (0x1e0)
#ifndef STANDARD_MEMORY_BIOS_CALL
	movb	%al, (E820NR)
# Try three different memory detection schemes.  First, try
# e820h, which lets us assemble a memory map, then try e801h,
# which returns a 32-bit memory size, and finally 88h, which
# returns 0-64m

# method E820H:
# the memory map from hell.  e820h returns memory classified into
# a whole bunch of different types, and allows memory holes and
# everything.  We scan through this memory map and build a list
# of the first 32 memory areas, which we return at [E820MAP].
# This is documented at http://www.teleport.com/~acpi/acpihtml/topic245.htm

#define SMAP  0x534d4150

meme820:
	xorl	%ebx, %ebx			# continuation counter
	movw	$E820MAP, %di			# point into the whitelist
						# so we can have the bios
						# directly write into it.

jmpe820:
	movl	$0x0000e820, %eax		# e820, upper word zeroed
	movl	$SMAP, %edx			# ascii 'SMAP'
	movl	$20, %ecx			# size of the e820rec
	pushw	%ds				# data record.
	popw	%es
	int	$0x15				# make the call
	jc	bail820				# fall to e801 if it fails

	cmpl	$SMAP, %eax			# check the return is `SMAP'
	jne	bail820				# fall to e801 if it fails

#	cmpl	$1, 16(%di)			# is this usable memory?
#	jne	again820

	# If this is usable memory, we save it by simply advancing %di by
	# sizeof(e820rec).
	#
good820:
	movb	(E820NR), %al			# up to 32 entries
	cmpb	$E820MAX, %al
	jnl	bail820

	incb	(E820NR)
	movw	%di, %ax
	addw	$20, %ax
	movw	%ax, %di
again820:
	cmpl	$0, %ebx			# check to see if
	jne	jmpe820				# %ebx is set to EOF
bail820:


# method E801H:
# memory size is in 1k chunksizes, to avoid confusing loadlin.
# we store the 0xe801 memory size in a completely different place,
# because it will most likely be longer than 16 bits.
# (use 1e0 because that's what Larry Augustine uses in his
# alternative new memory detection scheme, and it's sensible
# to write everything into the same place.)

meme801:
	stc					# fix to work around buggy
	xorw	%cx,%cx				# BIOSes which dont clear/set
	xorw	%dx,%dx				# carry on pass/error of
						# e801h memory size call
						# or merely pass cx,dx though
						# without changing them.
	movw	$0xe801, %ax
	int	$0x15
	jc	mem88

	cmpw	$0x0, %cx			# Kludge to handle BIOSes
	jne	e801usecxdx			# which report their extended
	cmpw	$0x0, %dx			# memory in AX/BX rather than
	jne	e801usecxdx			# CX/DX.  The spec I have read
	movw	%ax, %cx			# seems to indicate AX/BX 
	movw	%bx, %dx			# are more reasonable anyway...

e801usecxdx:
	andl	$0xffff, %edx			# clear sign extend
	shll	$6, %edx			# and go from 64k to 1k chunks
	movl	%edx, (0x1e0)			# store extended memory size
	andl	$0xffff, %ecx			# clear sign extend
 	addl	%ecx, (0x1e0)			# and add lower memory into
						# total size.

# Ye Olde Traditional Methode.  Returns the memory size (up to 16mb or
# 64mb, depending on the bios) in ax.
mem88:

#endif
	movb	$0x88, %ah
	int	$0x15
	movw	%ax, (2)

# Set the keyboard repeat rate to the max
	movw	$0x0305, %ax
	xorw	%bx, %bx
	int	$0x16

# Check for video adapter and its parameters and allow the
# user to browse video modes.
	call	video				# NOTE: we need %ds pointing
						# to bootsector

# Get hd0 data...
	xorw	%ax, %ax
	movw	%ax, %ds
	ldsw	(4 * 0x41), %si
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	pushw	%ax
	movw	%ax, %es
	movw	$0x0080, %di
	movw	$0x10, %cx
	pushw	%cx
	cld
	rep
 	movsb
# Get hd1 data...
	xorw	%ax, %ax
	movw	%ax, %ds
	ldsw	(4 * 0x46), %si
	popw	%cx
	popw	%es
	movw	$0x0090, %di
	rep
	movsb
# Check that there IS a hd1 :-)
	movw	$0x01500, %ax
	movb	$0x81, %dl
	int	$0x13
	jc	no_disk1
	
	cmpb	$3, %ah
	je	is_disk1

no_disk1:
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax 		# aka INITSEG
	movw	%ax, %es
	movw	$0x0090, %di
	movw	$0x10, %cx
	xorw	%ax, %ax
	cld
	rep
	stosb
is_disk1:

# Check for PS/2 pointing device
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ax, %ds
	movw	$0, (0x1ff)			# default is no pointing device
	int	$0x11				# int 0x11: equipment list
	testb	$0x04, %al			# check if mouse installed
	jz	no_psmouse

	movw	$0xAA, (0x1ff)			# device present
no_psmouse:

# Now we want to move to protected mode ...
	cmpw	$0, %cs:realmode_swtch
	jz	rmodeswtch_normal

	lcall	%cs:realmode_swtch

	jmp	rmodeswtch_end

rmodeswtch_normal:
        pushw	%cs
	call	default_switch

rmodeswtch_end:
# we get the code32 start address and modify the below 'jmpi'
# (loader may have changed it)
	movl	%cs:code32_start, %eax
	movl	%eax, %cs:code32

# Now we move the system to its rightful place ... but we check if we have a
# big-kernel. In that case we *must* not move it ...
	testb	$LOADED_HIGH, %cs:loadflags
	jz	do_move0			# .. then we have a normal low
						# loaded zImage
						# .. or else we have a high
						# loaded bzImage
	jmp	end_move			# ... and we skip moving

do_move0:
	movw	$0x100, %ax			# start of destination segment
	movw	%cs, %bp			# aka SETUPSEG
	subw	$DELTA_INITSEG, %bp		# aka INITSEG
	movw	%cs:start_sys_seg, %bx		# start of source segment
	cld
do_move:
	movw	%ax, %es			# destination segment
	incb	%ah				# instead of add ax,#0x100
	movw	%bx, %ds			# source segment
	addw	$0x100, %bx
	subw	%di, %di
	subw	%si, %si
	movw 	$0x800, %cx
	rep
	movsw
	cmpw	%bp, %bx			# assume start_sys_seg > 0x200,
						# so we will perhaps read one
						# page more than needed, but
						# never overwrite INITSEG
						# because destination is a
						# minimum one page below source
	jb	do_move

end_move:
# then we load the segment descriptors
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
		
# Check whether we need to be downward compatible with version <=201
	cmpl	$0, cmd_line_ptr
	jne	end_move_self		# loader uses version >=202 features
	cmpb	$0x20, type_of_loader
	je	end_move_self		# bootsect loader, we know of it

# Boot loader doesnt support boot protocol version 2.02.
# If we have our code not at 0x90000, we need to move it there now.
# We also then need to move the params behind it (commandline)
# Because we would overwrite the code on the current IP, we move
# it in two steps, jumping high after the first one.
	movw	%cs, %ax
	cmpw	$SETUPSEG, %ax
	je	end_move_self

	cli					# make sure we really have
						# interrupts disabled !
						# because after this the stack
						# should not be used
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ss, %dx
	cmpw	%ax, %dx
	jb	move_self_1

	addw	$INITSEG, %dx
	subw	%ax, %dx			# this will go into %ss after
						# the move
move_self_1:
	movw	%ax, %ds
	movw	$INITSEG, %ax			# real INITSEG
	movw	%ax, %es
	movw	%cs:setup_move_size, %cx
	std					# we have to move up, so we use
						# direction down because the
						# areas may overlap
	movw	%cx, %di
	decw	%di
	movw	%di, %si
	subw	$move_self_here+0x200, %cx
	rep
	movsb
	ljmp	$SETUPSEG, $move_self_here

move_self_here:
	movw	$move_self_here+0x200, %cx
	rep
	movsb
	movw	$SETUPSEG, %ax
	movw	%ax, %ds
	movw	%dx, %ss
end_move_self:					# now we are at the right place
	lidt	idt_48				# load idt with 0,0
	xorl	%eax, %eax			# Compute gdt_base
	movw	%ds, %ax			# (Convert %ds:gdt to a linear ptr)
	shll	$4, %eax
	addl	$gdt, %eax
	movl	%eax, (gdt_48+2)
	lgdt	gdt_48				# load gdt with whatever is
						# appropriate

# that was painless, now we enable a20
	call	empty_8042

	movb	$0xD1, %al			# command write
	outb	%al, $0x64
	call	empty_8042

	movb	$0xDF, %al			# A20 on
	outb	%al, $0x60
	call	empty_8042

#
#	You must preserve the other bits here. Otherwise embarrasing things
#	like laptops powering off on boot happen. Corrected version by Kira
#	Brown from Linux 2.2
#
	inb	$0x92, %al			# 
	orb	$02, %al			# "fast A20" version
	outb	%al, $0x92			# some chips have only this

# wait until a20 really *is* enabled; it can take a fair amount of
# time on certain systems; Toshiba Tecras are known to have this
# problem.  The memory location used here (0x200) is the int 0x80
# vector, which should be safe to use.

	xorw	%ax, %ax			# segment 0x0000
	movw	%ax, %fs
	decw	%ax				# segment 0xffff (HMA)
	movw	%ax, %gs
a20_wait:
	incw	%ax				# unused memory location <0xfff0
	movw	%ax, %fs:(0x200)		# we use the "int 0x80" vector
	cmpw	%gs:(0x210), %ax		# and its corresponding HMA addr
	je	a20_wait			# loop until no longer aliased

# make sure any possible coprocessor is properly reset..
	xorw	%ax, %ax
	outb	%al, $0xf0
	call	delay

	outb	%al, $0xf1
	call	delay

# well, that went ok, I hope. Now we mask all interrupts - the rest
# is done in init_IRQ().
	movb	$0xFF, %al			# mask all interrupts for now
	outb	%al, $0xA1
	call	delay
	
	movb	$0xFB, %al			# mask all irq's but irq2 which
	outb	%al, $0x21			# is cascaded

# Well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x1000 (or the loader supplied one),
# in 32-bit protected mode.
#
# Note that the short jump isn't strictly needed, although there are
# reasons why it might be a good idea. It won't hurt in any case.
	movw	$1, %ax				# protected mode (PE) bit
	lmsw	%ax				# This is it!
	jmp	flush_instr

flush_instr:
	xorw	%bx, %bx			# Flag to indicate a boot
	xorl	%esi, %esi			# Pointer to real-mode code
	movw	%cs, %si
	subw	$DELTA_INITSEG, %si
	shll	$4, %esi			# Convert to 32-bit pointer
# NOTE: For high loaded big kernels we need a
#	jmpi    0x100000,__KERNEL_CS
#
#	but we yet haven't reloaded the CS register, so the default size 
#	of the target offset still is 16 bit.
#       However, using an operand prefix (0x66), the CPU will properly
#	take our 48 bit far pointer. (INTeL 80386 Programmer's Reference
#	Manual, Mixing 16-bit and 32-bit code, page 16-6)

	.byte 0x66, 0xea			# prefix + jmpi-opcode
code32:	.long	0x1000				# will be set to 0x100000
						# for big kernels
	.word	__KERNEL_CS

# Here's a bunch of information about your current kernel..
kernel_version:	.ascii	UTS_RELEASE
		.ascii	" ("
		.ascii	LINUX_COMPILE_BY
		.ascii	"@"
		.ascii	LINUX_COMPILE_HOST
		.ascii	") "
		.ascii	UTS_VERSION
		.byte	0

# This is the default real mode switch routine.
# to be called just before protected mode transition
default_switch:
	cli					# no interrupts allowed !
	movb	$0x80, %al			# disable NMI for bootup
						# sequence
	outb	%al, $0x70
	lret

# This routine only gets called, if we get loaded by the simple
# bootsect loader _and_ have a bzImage to load.
# Because there is no place left in the 512 bytes of the boot sector,
# we must emigrate to code space here.
bootsect_helper:
	cmpw	$0, %cs:bootsect_es
	jnz	bootsect_second

	movb	$0x20, %cs:type_of_loader
	movw	%es, %ax
	shrw	$4, %ax
	movb	%ah, %cs:bootsect_src_base+2
	movw	%es, %ax
	movw	%ax, %cs:bootsect_es
	subw	$SYSSEG, %ax
	lret					# nothing else to do for now

bootsect_second:
	pushw	%cx
	pushw	%si
	pushw	%bx
	testw	%bx, %bx			# 64K full?
	jne	bootsect_ex

	movw	$0x8000, %cx			# full 64K, INT15 moves words
	pushw	%cs
	popw	%es
	movw	$bootsect_gdt, %si
	movw	$0x8700, %ax
	int	$0x15
	jc	bootsect_panic			# this, if INT15 fails

	movw	%cs:bootsect_es, %es		# we reset %es to always point
	incb	%cs:bootsect_dst_base+2		# to 0x10000
bootsect_ex:
	movb	%cs:bootsect_dst_base+2, %ah
	shlb	$4, %ah				# we now have the number of
						# moved frames in %ax
	xorb	%al, %al
	popw	%bx
	popw	%si
	popw	%cx
	lret

bootsect_gdt:
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0

bootsect_src:
	.word	0xffff

bootsect_src_base:
	.byte	0x00, 0x00, 0x01		# base = 0x010000
	.byte	0x93				# typbyte
	.word	0				# limit16,base24 =0

bootsect_dst:
	.word	0xffff

bootsect_dst_base:
	.byte	0x00, 0x00, 0x10		# base = 0x100000
	.byte	0x93				# typbyte
	.word	0				# limit16,base24 =0
	.word	0, 0, 0, 0			# BIOS CS
	.word	0, 0, 0, 0			# BIOS DS

bootsect_es:
	.word	0

bootsect_panic:
	pushw	%cs
	popw	%ds
	cld
	leaw	bootsect_panic_mess, %si
	call	prtstr
	
bootsect_panic_loop:
	jmp	bootsect_panic_loop

bootsect_panic_mess:
	.string	"INT15 refuses to access high mem, giving up."

# This routine checks that the keyboard command queue is empty
# (after emptying the output buffers)
#
# Some machines have delusions that the keyboard buffer is always full
# with no keyboard attached...
#
# If there is no keyboard controller, we will usually get 0xff
# to all the reads.  With each IO taking a microsecond and
# a timeout of 100,000 iterations, this can take about half a
# second ("delay" == outb to port 0x80). That should be ok,
# and should also be plenty of time for a real keyboard controller
# to empty.
#

empty_8042:
	pushl	%ecx
	movl	$100000, %ecx

empty_8042_loop:
	decl	%ecx
	jz	empty_8042_end_loop

	call	delay

	inb	$0x64, %al			# 8042 status port
	testb	$1, %al				# output buffer?
	jz	no_output

	call	delay
	inb	$0x60, %al			# read it
	jmp	empty_8042_loop

no_output:
	testb	$2, %al				# is input buffer full?
	jnz	empty_8042_loop			# yes - loop
empty_8042_end_loop:
	popl	%ecx
	ret

# Read the cmos clock. Return the seconds in al
gettime:
	pushw	%cx
	movb	$0x02, %ah
	int	$0x1a
	movb	%dh, %al			# %dh contains the seconds
	andb	$0x0f, %al
	movb	%dh, %ah
	movb	$0x04, %cl
	shrb	%cl, %ah
	aad
	popw	%cx
	ret

# Delay is needed after doing I/O
delay:
	outb	%al,$0x80
	ret

# Descriptor tables
gdt:
	.word	0, 0, 0, 0			# dummy

	.word	0, 0, 0, 0			# unused

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9A00				# code read/exec
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9200				# data read/write
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)
# this is 64bit descriptor for code
	.word	0xFFFF
	.word	0
	.word	0x9A00				# code read/exec
	.word	0x00AF				# as above, but it is long mode and with D=0
						# it does not seem to do the trick.

idt_48:
	.word	0				# idt limit = 0
	.word	0, 0				# idt base = 0L
gdt_48:
	.word	0x8000				# gdt limit=2048,
						#  256 GDT entries

	.word	0, 0				# gdt base (filled in later)

# Include video setup & detection code

#include "video.S"

# Setup signature -- must be last
setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

# After this point, there is some free space which is used by the video mode
# handling code to store the temporary mode table (not used by the kernel).

modelist:

.text
endtext:
.data
enddata:
.bss
endbss:
