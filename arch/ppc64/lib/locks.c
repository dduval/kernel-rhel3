/*
 * Simple spin lock operations.
 *
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>, IBM
 *   Rework to support virtual processors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <asm/memory.h>
#include <asm/hvcall.h>
#include <asm/paca.h>

/*
 * The following define is being used to select basic or shared processor
 * locking when running on an RPA platform.  As we do more performance
 * tuning, I would expect this selection mechanism to change.  Dave E.
 */
#define SPLPAR_LOCKS

/*
 * With shared processors, it is helpful if we can identify the
 * lock holder.  For this reason we use 0x100 + smp_processor_id()
 * as the value to indicate that the lock is held.
 * Kernel modules compiled prior to this change will write 1
 * to indicate that the lock is held.
 */
#ifdef SPLPAR_LOCKS
#define OCCUPIED	(0x100 + smp_processor_id())
#else
#define OCCUPIED	1
#endif

int spin_trylock(spinlock_t *lock)
{
	unsigned int tmp;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%1		# spin_trylock\n"
"  	cmpwi	0,%0,0\n"
"	li	%0,0\n"
"	bne-	2f\n"
"	li	%0,1\n"
"	stwcx.	%2,0,%1\n"
"	bne-	1b\n"
"	isync\n"
"2:"
	: "=&r" (tmp)
	: "r" (&lock->lock), "r" (OCCUPIED)
	: "cr0", "memory");

	return tmp;
}

EXPORT_SYMBOL(spin_trylock);

#ifdef CONFIG_PPC_ISERIES
void spin_lock(spinlock_t *lock)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# spin_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%2)        # load the lock value\n\
	cmpwi	0,%0,0          # if not locked, try to acquire\n\
	beq-	2f\n\
	andi.	%1,%0,0x100	# is this the cpu number?\n\
	beq	1b		# just spin when don't know\n\
	rlwinm	3,%0,13,11,18	# form address of holder's paca\n\
	add	3,3,%4\n\
	lwz     5,%5(3)         # load yield counter\n\
	andi.   %1,5,1          # if even then spin\n\
	beq     1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz	%1,0(%2)        # reverify the lock holder\n\
	cmpw	%0,%1\n\
	bne	1b              # new holder so restart\n\
	li	3,0x25          # yield hcall 0x8-12 \n\
	rotrdi	3,3,1           #   put the bits in the right spot\n\
	rldic	4,%0,24,32      # processor number in top half\n\
	or	5,5,4           # r5 has yield cnt - or it in\n\
	li	4,2             # yield to processor\n\
	li	0,-1            # indicate an hcall\n\
	sc                      # do the hcall \n\
	b       1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%3,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&lock->lock), "r" (OCCUPIED), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r0", "r3", "r4", "r5", "ctr", "cr0", "cr1", "cr2", "cr3", "cr4",
	  "xer", "memory");
}
#else
#ifdef SPLPAR_LOCKS
void spin_lock(spinlock_t *lock)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# spin_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%2)        # load the lock value\n\
	cmpwi	0,%0,0          # if not locked, try to acquire\n\
	beq-	2f\n\
	andi.	%1,%0,0x100	# is this the cpu number?\n\
	beq	1b		# just spin when don't know\n\
	rldic	3,%0,13,11+32	# form address of holder's paca\n\
	add	3,3,%4\n\
	lwz	5,%5(3)		# load dispatch counter\n\
	andi.	%1,5,1          # if even then spin\n\
	beq	1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz	%1,0(%2)	# reverify the lock holder\n\
	cmpw	%0,%1\n\
	bne	1b              # new holder so restart\n\
	li	3,0xE4          # give up the cycles H_CONFER\n\
	clrldi	4,%0,56		# processor number\n\
				# r5 has dispatch cnt already\n"
	HVSC
"       b	1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%3,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&lock->lock), "r" (OCCUPIED), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r3", "r4", "r5", "cr0", "cr1", "ctr", "xer", "memory");
}
#else
void spin_lock(spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
       "b	2f		# spin_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%1)	# load the lock value\n\
	cmpwi	0,%0,0		# if not locked, try to acquire\n\
	bne+	1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%2,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp)
	: "b" (&lock->lock), "r" (OCCUPIED)
	: "cr0", "memory");
}
#endif
#endif

EXPORT_SYMBOL(spin_lock);

/*
 * This value is used to indicate that a write lock is held.
 */
#ifdef SPLPAR_LOCKS
#define WRITE_OCCUPIED	(smp_processor_id() - 0x100)
#else
#define WRITE_OCCUPIED	(-1)
#endif

int read_trylock(rwlock_t *rw)
{
	unsigned int tmp;
	unsigned int ret;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# read_trylock\n\
	li	%1,0\n\
	extsw	%0,%0\n\
	addic.	%0,%0,1\n\
	ble-	2f\n\
	stwcx.	%0,0,%2\n\
	bne-	1b\n\
	li	%1,1\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (ret)
	: "r" (&rw->lock)
	: "cr0", "memory", "xer");

	return ret;
}

EXPORT_SYMBOL(read_trylock);

#ifdef CONFIG_PPC_ISERIES
void read_lock(rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# read_lock\n\
1:"
	HMT_LOW
"	lwz	%0,0(%2)\n\
	cmpwi	0,%0,-1\n\
	bgt-	2f\n\
	beq-	1b		# spin if holder not known\n\
	rldic	3,%0,13,11+32	# form address of holder's paca\n\
	add	3,3,%3\n\
	lwz     5,%4(3)		# load yield counter\n\
	andi.   %1,5,1          # if even then spin\n\
	beq     1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz	%1,0(%2)	# reverify the lock holder\n\
	cmpw    %0,%1\n\
	bne     1b              # new holder so restart\n\
	li      3,0x25          # yield hcall 0x8-12 \n\
	rotrdi  3,3,1           #   put the bits in the right spot\n\
	rldic	4,%0,24,32      # processor number in top half\n\
	or      5,5,4           # r5 has yield cnt - or it in\n\
	li      4,2             # yield to processor\n\
	li      0,-1            # indicate an hcall\n\
	sc                      # do the hcall \n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	extsw	%0,%0\n\
	addic.	%0,%0,1\n\
	ble-	1b\n\
	stwcx.	%0,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&rw->lock), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r0", "r3", "r4", "r5", "ctr", "cr0", "cr1", "cr2", "cr3", "cr4",
	  "xer", "memory");
}
#else
#ifdef SPLPAR_LOCKS
void read_lock(rwlock_t *rw)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# read_lock\n\
1:"
	HMT_LOW
"	lwz	%0,0(%2)\n\
	cmpwi	0,%0,-1\n\
	bgt-	2f\n\
	beq-	1b		# spin if holder not known\n\
	rldic	3,%0,13,11+32	# form address of holder's paca\n\
	add	3,3,%3\n\
	lwz     5,%4(3)		# load dispatch counter\n\
	andi.   %1,5,1          # if even then spin\n\
	beq     1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz     %1,0(%2)	# reverify the lock holder\n\
	cmpw	%0,%1\n\
	bne     1b              # new holder so restart\n\
	li      3,0xE4          # give up the cycles H_CONFER\n\
	clrldi	4,%0,56		# processor number\n\
				# r5 has dispatch cnt already\n"
	HVSC
"2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	extsw	%0,%0\n\
	addic.	%0,%0,1\n\
	ble-	1b\n\
	stwcx.	%0,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&rw->lock), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r3", "r4", "r5", "cr0", "cr1", "ctr", "xer", "memory");
}
#else
void read_lock(rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
	"b	2f		# read_lock\n\
1:"
	HMT_LOW
"	lwz	%0,0(%1)\n\
	cmpwi	0,%0,0\n\
	blt+	1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%1\n\
	extsw	%0,%0\n\
	addic.	%0,%0,1\n\
	ble-	1b\n\
	stwcx.	%0,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp)
	: "b" (&rw->lock)
	: "cr0", "memory", "xer");
}
#endif
#endif

EXPORT_SYMBOL(read_lock);

void read_unlock(rwlock_t *rw)
{
	unsigned int tmp;

	__asm__ __volatile__(
	"lwsync			# read_unlock\n\
1:	lwarx	%0,0,%1\n\
	addic	%0,%0,-1\n\
	stwcx.	%0,0,%1\n\
	bne-	1b"
	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "memory");
}

EXPORT_SYMBOL(read_unlock);

int write_trylock(rwlock_t *rw)
{
	unsigned int tmp;
	unsigned int ret;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# write_trylock\n\
	cmpwi	0,%0,0\n\
	li	%1,0\n\
	bne-	2f\n\
	stwcx.	%3,0,%2\n\
	bne-	1b\n\
	li	%1,1\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (ret)
	: "r" (&rw->lock), "r" (WRITE_OCCUPIED)
	: "cr0", "memory");

	return ret;
}

EXPORT_SYMBOL(write_trylock);

#ifdef CONFIG_PPC_ISERIES
void write_lock(rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# spin_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%2)	# load the lock value\n\
	cmpwi	0,%0,0		# if not locked(0), try to acquire\n\
	beq-	2f\n\
	cmpwi	0,%0,-1		# do we know who holds the lock?\n\
	bge	1b              # if not then spin\n\
	rldic	3,%0,13,11+32	# form address of holder's paca\n\
	add	3,3,%4\n\
	lwz     5,%5(3)		# load yield counter\n\
	andi.   %1,5,1          # if even then spin\n\
	beq     1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz	%1,0(%2)	# reverify the lock holder\n\
	cmpw	%0,%1\n\
	bne	1b              # new holder so restart\n\
	li      3,0x25          # yield hcall 0x8-12 \n\
	rotrdi  3,3,1           #   put the bits in the right spot\n\
	rldic	4,%0,24,32      # processor number in top half\n\
	or      5,5,4           # r5 has yield cnt - or it in\n\
	li      4,2             # yield to processor\n\
	li      0,-1            # indicate an hcall\n\
	sc                      # do the hcall \n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%3,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&rw->lock), "r" (WRITE_OCCUPIED), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r0", "r3", "r4", "r5", "ctr", "cr0", "cr1", "cr2", "cr3", "cr4",
	  "xer", "memory");
}
#else
#ifdef SPLPAR_LOCKS
void write_lock(rwlock_t *rw)
{
	unsigned int tmp, tmp2;

	__asm__ __volatile__(
	"b	2f		# spin_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%2)	# load the lock value\n\
	cmpwi	0,%0,0          # if not locked(0), try to acquire\n\
	beq-	2f\n\
	cmpwi	0,%0,-1		# do we know lock holder?\n\
	bge	1b		# spin if not\n\
	rldic	3,%0,13,11+32	# form address of holder's paca\n\
	add	3,3,%4\n\
	lwz     5,%5(3)		# load dispatch counter\n\
	andi.   %1,5,1          # if even then spin\n\
	beq     1b\n\
	lwsync                  # if odd, give up cycles\n\
	lwz	%1,0(%2)	# reverify the lock holder\n\
	cmpw	%0,%1\n\
	bne     1b              # new holder so restart\n\
	li      3,0xE4          # give up the cycles H_CONFER\n\
	clrldi	4,%0,56		# processor number\n\
				# r5 has dispatch cnt already\n"
	HVSC
"	b	1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%3,0,%2\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp), "=&r" (tmp2)
	: "b" (&rw->lock), "r" (WRITE_OCCUPIED), "r" (paca),
	  "i" (offsetof(struct paca_struct, xLpPaca.xYieldCount))
	: "r3", "r4", "r5", "cr0", "cr1", "ctr", "xer", "memory");
}
#else
void write_lock(rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__(
	"b	2f		# write_lock\n\
1:"
	HMT_LOW
"       lwz	%0,0(%1)        # load the lock value\n\
	cmpwi	0,%0,0          # if not locked(0), try to acquire\n\
	bne+	1b\n\
2: \n"
	HMT_MEDIUM
" 	lwarx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n\
	stwcx.	%2,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r" (tmp)
	: "b" (&rw->lock), "r" (WRITE_OCCUPIED)
	: "cr0", "memory");
}
#endif
#endif

EXPORT_SYMBOL(write_lock);
