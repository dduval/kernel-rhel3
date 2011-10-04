#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.  
 *
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Type of int is used as a full 64b word is not necessary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#ifdef __KERNEL__
#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 }

#define spin_is_locked(x)	((x)->lock != 0)

extern int spin_trylock(spinlock_t *lock);

extern void spin_lock(spinlock_t *lock);

static __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("lwsync	# spin_unlock": : :"memory");
	lock->lock = 0;
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 * Read-write lock states:
 *  0:	unlocked
 *  positive: reader count
 *  negative: write locked.
 *
 * Old modules put -1 in to indicate a write lock.
 * Kernel code puts smp_processor_id() - 0x100 in, giving
 * us the opportunity to optimize performance on shared processors.
 */
typedef struct {
	volatile signed int lock;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

extern int read_trylock(rwlock_t *rw);

extern void read_lock(rwlock_t *rw);

extern void read_unlock(rwlock_t *rw);

extern int write_trylock(rwlock_t *rw);

extern void write_lock(rwlock_t *rw);

static __inline__ void write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("lwsync		# write_unlock": : :"memory");
	rw->lock = 0;
}

static __inline__ int is_read_locked(rwlock_t *rw)
{
	return rw->lock > 0;
}

static __inline__ int is_write_locked(rwlock_t *rw)
{
	return rw->lock < 0;
}

#define spin_lock_init(x)      do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)
#define spin_unlock_wait(x)    do { barrier(); } while(spin_is_locked(x))

#define rwlock_init(x)         do { *(x) = RW_LOCK_UNLOCKED; } while(0)

#define rwlock_is_locked(x)     ((x)->lock)


#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
