/*
 *  linux/kernel/timer.c
 *
 *  Kernel internal timers, kernel timekeeping, basic process system calls
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 *
 *  1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 *  1998-12-24  Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *              serialize accesses to xtime/lost_ticks).
 *                              Copyright (C) 1998  Andrea Arcangeli
 *  1999-03-10  Improved NTP compatibility by Ulrich Windl
 *  2000-10-05  Implemented scalable SMP per-CPU timer handling.
 *                              Copyright (C) 2000, 2001, 2002  Ingo Molnar
 *              Designed by David S. Miller, Alexey Kuznetsov and Ingo Molnar
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/timex.h>
#include <linux/process_timing.h>
#include <linux/linkage.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>

#include <asm/uaccess.h>

struct kernel_stat_percpu kstat_percpu[NR_CPUS] ____cacheline_aligned;

/*
 * Timekeeping variables
 */

long tick = (1000000 + HZ/2) / HZ;	/* timer interrupt period */

/* The current time */
struct timeval xtime __cacheline_aligned;

/* Don't completely fail for HZ > 500.  */
int tickadj = 500/HZ ? : 1;		/* microsecs */

/* The function pointers in case we are using the accurate process accounting */
struct process_timing_entry process_timing __cacheline_aligned;

DECLARE_TASK_QUEUE(tq_timer);
DECLARE_TASK_QUEUE(tq_immediate);

/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
int time_state = TIME_OK;		/* clock synchronization status	*/
int time_status = STA_UNSYNC;		/* clock status bits		*/
long time_offset;			/* time adjustment (us)		*/
long time_constant = 2;			/* pll time constant		*/
long time_tolerance = MAXFREQ;		/* frequency tolerance (ppm)	*/
long time_precision = 1;		/* clock precision (us)		*/
long time_maxerror = NTP_PHASE_LIMIT;	/* maximum error (us)		*/
long time_esterror = NTP_PHASE_LIMIT;	/* estimated error (us)		*/
long time_phase;			/* phase offset (scaled us)	*/
long time_freq = ((1000000 + HZ/2) % HZ - HZ/2) << SHIFT_USEC;
					/* frequency offset (scaled ppm)*/
long time_adj;				/* tick adjust (scaled 1 / HZ)	*/
long time_reftime;			/* time at last adjustment (s)	*/

long time_adjust;
long time_adjust_step;

unsigned long event;

extern int do_setitimer(int, struct itimerval *, struct itimerval *);

unsigned long volatile jiffies __cacheline_aligned;
/*
 * per-CPU timer vector definitions:
 */
#define TVN_BITS 6
#define TVR_BITS 8
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

#define TIMER_MAGIC	0x4b87ad6e

typedef struct tvec_s {
	struct list_head vec[TVN_SIZE];
} tvec_t;

typedef struct tvec_root_s {
	struct list_head vec[TVR_SIZE];
} tvec_root_t;

struct tvec_t_base_s {
	spinlock_t lock;
	unsigned long timer_jiffies;
	struct timer_list *running_timer;
	tvec_root_t tv1;
	tvec_t tv2;
	tvec_t tv3;
	tvec_t tv4;
	tvec_t tv5;
	unsigned int magic;
} ____cacheline_aligned_in_smp;

typedef struct tvec_t_base_s tvec_base_t;

static inline void set_running_timer(tvec_base_t *base,
					struct timer_list *timer)
{
#ifdef CONFIG_SMP
	base->running_timer = timer;
#endif
}

static tvec_base_t tvec_bases[NR_CPUS] =
	{ [ 0 ... NR_CPUS-1] = { .lock = SPIN_LOCK_UNLOCKED} };

/*
 * Either enable or disable the accurate process timing code.
 * The real work for the accounting code is handled almost entirely
 * in the arch/ subdirectory since making this go fast is *very*
 * hardware dependant.
 */
int process_timing_setup_flags;

static int __init  setup_process_timing(char *str) {
	while(str != NULL) {
		if (strncmp(str, "irq", 3) == 0)
			process_timing_setup_flags |= PT_FLAGS_IRQ;
		if (strncmp(str, "softirq", 7) == 0)
			process_timing_setup_flags |= PT_FLAGS_SOFTIRQ;
		if (strncmp(str, "process", 7) == 0)
			process_timing_setup_flags |= PT_FLAGS_PROCESS;
		if (strncmp(str, "all_processes", 13) == 0)
			process_timing_setup_flags |= PT_FLAGS_PROCESS |
				PT_FLAGS_ALL_PROCESS;
		if (strncmp(str, "everything", 10) == 0)
			process_timing_setup_flags = PT_FLAGS_EVERYTHING;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}
__setup("process_timing=", setup_process_timing);

static inline void check_kernel_timer(struct timer_list *timer)
{
	tvec_base_t *base = timer->base;

	BUG_ON(timer->magic);
	BUG_ON(base && (base->magic != TIMER_MAGIC));
}

static void internal_add_timer(tvec_base_t *base, struct timer_list *timer)
{
	unsigned long expires = timer->expires;
	unsigned long idx = expires - base->timer_jiffies;
	struct list_head *vec;

	if (idx < TVR_SIZE) {
		int i = expires & TVR_MASK;
		vec = base->tv1.vec + i;
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
		int i = (expires >> TVR_BITS) & TVN_MASK;
		vec = base->tv2.vec + i;
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		vec = base->tv3.vec + i;
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		vec = base->tv4.vec + i;
	} else if ((signed long) idx < 0) {
		/*
		 * Can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		vec = base->tv1.vec + (base->timer_jiffies & TVR_MASK);
	} else {
		int i;
		/* If the timeout is larger than 0xffffffff on 64-bit
		 * architectures then we use the maximum timeout:
		 */
		if (idx > 0xffffffffUL) {
			idx = 0xffffffffUL;
			expires = idx + base->timer_jiffies;
		}
		i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		vec = base->tv5.vec + i;
	}
	/*
	 * Timers are FIFO:
	 */
	list_add_tail(&timer->entry, vec);
}

static inline int trylock_timer(struct timer_list *timer)
{
	return test_and_set_bit(0, &timer->lock);
}

static inline void lock_timer(struct timer_list *timer)
{
	while (unlikely(trylock_timer(timer)))
		cpu_relax();
}

static inline void unlock_timer(struct timer_list *timer)
{
	if (unlikely(!test_and_clear_bit(0, &timer->lock)))
		BUG();
}

static int __mod_timer(struct timer_list *timer, unsigned long expires)
{
	tvec_base_t *old_base, *new_base;
	unsigned long flags;
	int ret = 0;

	local_irq_save(flags);
	lock_timer(timer);
	new_base = tvec_bases + smp_processor_id();
repeat:
	old_base = timer->base;

	/*
	 * Prevent deadlocks via ordering by old_base < new_base.
	 */
	if (old_base && (new_base != old_base)) {
		if (old_base < new_base) {
			spin_lock(&new_base->lock);
			spin_lock(&old_base->lock);
		} else {
			spin_lock(&old_base->lock);
			spin_lock(&new_base->lock);
		}
		/*
		 * The timer base might have been cancelled while we were
		 * trying to take the lock(s):
		 */
		if (timer->base != old_base) {
			spin_unlock(&new_base->lock);
			spin_unlock(&old_base->lock);
			goto repeat;
		}
	} else {
		spin_lock(&new_base->lock);
		if (timer->base != old_base) {
			spin_unlock(&new_base->lock);
			goto repeat;
		}
	}

	/*
	 * Delete the previous timeout (if there was any), and install
	 * the new one:
	 */
	if (old_base) {
		check_kernel_timer(timer);
		list_del(&timer->entry);
		ret = 1;
	}
	timer->expires = expires;
	internal_add_timer(new_base, timer);
	timer->base = new_base;

	if (old_base && (new_base != old_base))
		spin_unlock(&old_base->lock);
	spin_unlock(&new_base->lock);
	unlock_timer(timer);
	local_irq_restore(flags);

	return ret;
}


/***
 * timer_pending - is a timer pending?
 * @timer: the timer in question
 *
 * timer_pending will tell whether a given timer is currently pending,
 * or not. Callers must ensure serialization wrt. other operations done
 * to this timer, eg. interrupt contexts, or other CPUs on SMP.
 *
 * return value: 1 if the timer is pending, 0 if not.
 */
inline int timer_pending(struct timer_list * timer)
{
	check_kernel_timer(timer);
	return timer->base != NULL;
}

/***
 * add_timer - start a timer
 * @timer: the timer to be added
 *
 * The kernel will do a ->function(->data) callback from the
 * timer interrupt at the ->expired point in the future. The
 * current time is 'jiffies'.
 *
 * The timer's ->expired, ->function (and if the handler uses it, ->data)
 * fields must be set prior calling this function.
 *
 * Timers with an ->expired field in the past will be executed in the next
 * timer tick. It's illegal to add an already pending timer.
 */
void add_timer(struct timer_list *timer)
{
  	BUG_ON(timer_pending(timer) || !timer->function);
	check_kernel_timer(timer);

	__mod_timer(timer, timer->expires);
}

/***
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 *
 * mod_timer is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list *timer, unsigned long expires)
{
	BUG_ON(!timer->function);
	check_kernel_timer(timer);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && timer_pending(timer))
		return 1;

	return __mod_timer(timer, expires);
}

/***
 * del_timer - deactive a timer.
 * @timer: the timer to be deactivated
 *
 * del_timer() deactivates a timer - this works on both active and inactive
 * timers.
 *
 * The function returns whether it has deactivated a pending timer or not.
 * (ie. del_timer() of an inactive timer returns 0, del_timer() of an
 * active timer returns 1.)
 */
int del_timer(struct timer_list *timer)
{
	unsigned long flags;
	tvec_base_t *base;

	check_kernel_timer(timer);

repeat:
 	base = timer->base;
	if (!base)
		return 0;
	spin_lock_irqsave(&base->lock, flags);
	if (base != timer->base) {
		spin_unlock_irqrestore(&base->lock, flags);
		goto repeat;
	}
	list_del(&timer->entry);
	smp_wmb();
	timer->base = NULL;
	spin_unlock_irqrestore(&base->lock, flags);

	return 1;
}

#ifdef CONFIG_SMP
/***
 * del_timer_sync - deactivate a timer and wait for the handler to finish.
 * @timer: the timer to be deactivated
 *
 * This function only differs from del_timer() on SMP: besides deactivating
 * the timer it also makes sure the handler has finished executing on other
 * CPUs.
 *
 * Synchronization rules: callers must prevent restarting of the timer,
 * otherwise this function is meaningless. It must not be called from
 * interrupt contexts. Upon exit the timer is not queued and the handler
 * is not running on any CPU.
 *
 * The function returns whether it has deactivated a pending timer or not.
 */
int del_timer_sync(struct timer_list *timer)
{
	tvec_base_t *base;
	int i, ret;

del_again:
	ret = del_timer(timer);

	if (unlikely(!ret)) {
		for (i = 0; i < NR_CPUS; i++) {
			if (!cpu_online(i))
				continue;

			base = tvec_bases + i;
			if (base->running_timer == timer) {
				while (base->running_timer == timer)
					cpu_relax();
				break;
			}
		}
		smp_rmb();
		if (timer_pending(timer))
			goto del_again;
	}

	return ret;
}
#endif


static int cascade(tvec_base_t *base, tvec_t *tv, int index)
{
	/* cascade all the timers from tv up one level */
	struct list_head *head, *curr;

	head = tv->vec + index;
	curr = head->next;
	/*
	 * We are removing _all_ timers from the list, so we don't  have to
	 * detach them individually, just clear the list afterwards.
	 */
	while (curr != head) {
		struct timer_list *timer;

		timer = list_entry(curr, struct timer_list, entry);
		BUG_ON(timer->base != base);
		check_kernel_timer(timer);
		curr = curr->next;
		internal_add_timer(base, timer);
	}
	INIT_LIST_HEAD(head);

	return index;
}

/***
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 *
 * This function cascades all vectors and executes all expired timer
 * vectors.
 */
#define INDEX(N) (base->timer_jiffies >> (TVR_BITS + N * TVN_BITS)) & TVN_MASK

static inline void __run_timers(tvec_base_t *base)
{
	struct timer_list *timer;

	spin_lock_irq(&base->lock);
	while (time_after_eq(jiffies, base->timer_jiffies)) {
		struct list_head work_list = LIST_HEAD_INIT(work_list);
		struct list_head *head = &work_list;
 		int index = base->timer_jiffies & TVR_MASK;
 
		/*
		 * Cascade timers:
		 */
		if (!index &&
			(!cascade(base, &base->tv2, INDEX(0))) &&
				(!cascade(base, &base->tv3, INDEX(1))) &&
					!cascade(base, &base->tv4, INDEX(2)))
			cascade(base, &base->tv5, INDEX(3));
		++base->timer_jiffies; 
		list_splice_init(base->tv1.vec + index, &work_list);
repeat:
		if (!list_empty(head)) {
			void (*fn)(unsigned long);
			unsigned long data;

			timer = list_entry(head->next,struct timer_list,entry);
			check_kernel_timer(timer);
 			fn = timer->function;
 			data = timer->data;

			list_del(&timer->entry);
			set_running_timer(base, timer);
			smp_wmb();
			timer->base = NULL;
			spin_unlock_irq(&base->lock);
			fn(data);
			spin_lock_irq(&base->lock);
			goto repeat;
		}
	}
	set_running_timer(base, NULL);
	spin_unlock_irq(&base->lock);
}

#ifdef CONFIG_NO_IDLE_HZ

/*
 * Find out when the next timer event is due to happen. This
 * is used on S/390 to stop all activity when all cpus are idle.
 * This is called with all locks relevant to stop_hz_timer taken
 * and interrupts closed, but it has to take locks in tvecs.
 *
 * Scan first 256 jiffies on all CPUs, then next 63*256 jiffies, etc.
 */
unsigned long next_timer_event(void)
{
	tvec_base_t *base;
	struct list_head *list;
	struct timer_list *nte;
	unsigned long expires;
	tvec_t *varray[4];
	int i, j;

	base = tvec_bases + smp_processor_id();
	spin_lock(&base->lock);
	expires = base->timer_jiffies + (LONG_MAX >> 1);
	list = 0;
	
	/* Look for timer events in tv1. */
	j = base->timer_jiffies & TVR_MASK;
	do {
		list_for_each_entry(nte, base->tv1.vec + j, entry) {
			expires = nte->expires;
			if (j < (base->timer_jiffies & TVR_MASK))
				list = base->tv2.vec + (INDEX(0));
			goto found;
		}
		j = (j + 1) & TVR_MASK;
	} while (j != (base->timer_jiffies & TVR_MASK));
	
	/* Check tv2-tv5. */
	varray[0] = &base->tv2;
	varray[1] = &base->tv3;
	varray[2] = &base->tv4;
	varray[3] = &base->tv5;
	for (i = 0; i < 4; i++) {
		j = INDEX(i);
		do {
			if (list_empty(varray[i]->vec + j)) {
				j = (j + 1) & TVN_MASK;
				continue;
			}
			list_for_each_entry(nte, varray[i]->vec + j, entry)
				if (time_before(nte->expires, expires))
					expires = nte->expires;
			if (j < (INDEX(i)) && i < 3)
				list = varray[i + 1]->vec + (INDEX(i + 1));
			goto found;
		} while (j != (INDEX(i)));
	}
found:
	if (list) {
		/*
		 * The search wrapped. We need to look at the next list
		 * from next tv element that would cascade into tv element
		 * where we found the timer element.
		 */
		list_for_each_entry(nte, list, entry) {
			if (time_before(nte->expires, expires))
				expires = nte->expires;
		}
	}
	spin_unlock(&base->lock);
	return expires;
}
#endif

spinlock_t tqueue_lock = SPIN_LOCK_UNLOCKED;

void tqueue_bh(void)
{
	run_task_queue(&tq_timer);
}

void immediate_bh(void)
{
	run_task_queue(&tq_immediate);
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 */
static void second_overflow(void)
{
    long ltemp;

    /* Bump the maxerror field */
    time_maxerror += time_tolerance >> SHIFT_USEC;
    if ( time_maxerror > NTP_PHASE_LIMIT ) {
	time_maxerror = NTP_PHASE_LIMIT;
	time_status |= STA_UNSYNC;
    }

    /*
     * Leap second processing. If in leap-insert state at
     * the end of the day, the system clock is set back one
     * second; if in leap-delete state, the system clock is
     * set ahead one second. The microtime() routine or
     * external clock driver will insure that reported time
     * is always monotonic. The ugly divides should be
     * replaced.
     */
    switch (time_state) {

    case TIME_OK:
	if (time_status & STA_INS)
	    time_state = TIME_INS;
	else if (time_status & STA_DEL)
	    time_state = TIME_DEL;
	break;

    case TIME_INS:
	if (xtime.tv_sec % 86400 == 0) {
	    xtime.tv_sec--;
	    time_state = TIME_OOP;
	    printk(KERN_NOTICE "Clock: inserting leap second 23:59:60 UTC\n");
	}
	break;

    case TIME_DEL:
	if ((xtime.tv_sec + 1) % 86400 == 0) {
	    xtime.tv_sec++;
	    time_state = TIME_WAIT;
	    printk(KERN_NOTICE "Clock: deleting leap second 23:59:59 UTC\n");
	}
	break;

    case TIME_OOP:
	time_state = TIME_WAIT;
	break;

    case TIME_WAIT:
	if (!(time_status & (STA_INS | STA_DEL)))
	    time_state = TIME_OK;
    }

    /*
     * Compute the phase adjustment for the next second. In
     * PLL mode, the offset is reduced by a fixed factor
     * times the time constant. In FLL mode the offset is
     * used directly. In either mode, the maximum phase
     * adjustment for each second is clamped so as to spread
     * the adjustment over not more than the number of
     * seconds between updates.
     */
    if (time_offset < 0) {
	ltemp = -time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset += ltemp;
	time_adj = -ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    } else {
	ltemp = time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset -= ltemp;
	time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    }

    /*
     * Compute the frequency estimate and additional phase
     * adjustment due to frequency error for the next
     * second. When the PPS signal is engaged, gnaw on the
     * watchdog counter and update the frequency computed by
     * the pll and the PPS signal.
     */
    pps_valid++;
    if (pps_valid == PPS_VALID) {	/* PPS signal lost */
	pps_jitter = MAXTIME;
	pps_stabil = MAXFREQ;
	time_status &= ~(STA_PPSSIGNAL | STA_PPSJITTER |
			 STA_PPSWANDER | STA_PPSERROR);
    }
    ltemp = time_freq + pps_freq;
    if (ltemp < 0)
	time_adj -= -ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);
    else
	time_adj += ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);

#if HZ == 100
    /* Compensate for (HZ==100) != (1 << SHIFT_HZ).
     * Add 25% and 3.125% to get 128.125; => only 0.125% error (p. 14)
     */
    if (time_adj < 0)
	time_adj -= (-time_adj >> 2) + (-time_adj >> 5);
    else
	time_adj += (time_adj >> 2) + (time_adj >> 5);
#endif
}

/* in the NTP reference this is called "hardclock()" */
static void update_wall_time_one_tick(void)
{
	if ( (time_adjust_step = time_adjust) != 0 ) {
	    /* We are doing an adjtime thing. 
	     *
	     * Prepare time_adjust_step to be within bounds.
	     * Note that a positive time_adjust means we want the clock
	     * to run faster.
	     *
	     * Limit the amount of the step to be in the range
	     * -tickadj .. +tickadj
	     */
	     if (time_adjust > tickadj)
		time_adjust_step = tickadj;
	     else if (time_adjust < -tickadj)
		time_adjust_step = -tickadj;
	     
	    /* Reduce by this step the amount of time left  */
	    time_adjust -= time_adjust_step;
	}
	xtime.tv_usec += tick + time_adjust_step;
	/*
	 * Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if (time_phase <= -FINEUSEC) {
		long ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		xtime.tv_usec -= ltemp;
	}
	else if (time_phase >= FINEUSEC) {
		long ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		xtime.tv_usec += ltemp;
	}
}

/*
 * Using a loop looks inefficient, but "ticks" is
 * usually just one (we shouldn't be losing ticks,
 * we're doing this this way mainly for interrupt
 * latency reasons, not because we think we'll
 * have lots of lost timer ticks
 */
static void update_wall_time(unsigned long ticks)
{
	do {
		ticks--;
		update_wall_time_one_tick();
	} while (ticks);

	while (xtime.tv_usec >= 1000000) {
	    xtime.tv_usec -= 1000000;
	    xtime.tv_sec++;
	    second_overflow();
	}
#ifdef ARCH_UPDATE_WALL_TIME
	ARCH_UPDATE_WALL_TIME();
#endif
}

static inline void do_process_times(struct task_struct *p,
				    struct kernel_stat_tick_times *time)
{
	struct kernel_timeval psecs;

	kernel_timeval_add_usec(&p->utime, time->u_usec + time->n_usec);
	kernel_timeval_add_usec(&p->group_leader->group_utime, time->u_usec +
							       time->n_usec);
	kernel_timeval_add_usec(&p->stime, time->s_usec);
	kernel_timeval_add_usec(&p->group_leader->group_stime, time->s_usec);
	kernel_timeval_add(&psecs, &p->utime, &p->stime);

	if (psecs.tv_sec > p->rlim[RLIMIT_CPU].rlim_cur &&
	    psecs.tv_sec != p->last_sigxcpu) {
		/* Send SIGXCPU every second.. */
		send_sig(SIGXCPU, p, 1);
		p->last_sigxcpu = psecs.tv_sec;
		/* and SIGKILL when we go over max.. */
		if (psecs.tv_sec > p->rlim[RLIMIT_CPU].rlim_max)
			send_sig(SIGKILL, p, 1);
	}
}

static inline void do_it_virt(struct task_struct * p, unsigned long ticks)
{
	unsigned long it_virt = p->it_virt_value;

	if (it_virt) {
		it_virt -= ticks;
		if (!it_virt) {
			it_virt = p->it_virt_incr;
			send_sig(SIGVTALRM, p, 1);
		}
		p->it_virt_value = it_virt;
	}
}

static inline void do_it_prof(struct task_struct *p)
{
	unsigned long it_prof = p->it_prof_value;

	if (it_prof) {
		if (--it_prof == 0) {
			it_prof = p->it_prof_incr;
			send_sig(SIGPROF, p, 1);
		}
		p->it_prof_value = it_prof;
	}
}


void update_one_process(struct task_struct *p,
			struct kernel_stat_tick_times *time, int cpu)
{
	kernel_timeval_add_usec(&p->per_cpu_utime[cpu], time->u_usec + 
							time->n_usec);
	kernel_timeval_add_usec(&p->per_cpu_stime[cpu], time->s_usec);
	do_process_times(p, time);
}	

void update_process_time_intertick(struct task_struct *p,
				   struct kernel_stat_tick_times *time)
{
	int u_ticks;
	int s_ticks;
	int cpu = smp_processor_id();

	u_ticks = kernel_timeval_to_jiffies(&p->utime);
	s_ticks = kernel_timeval_to_jiffies(&p->stime);
	update_one_process(p, time, cpu);
	u_ticks = kernel_timeval_to_jiffies(&p->utime) - u_ticks;
	s_ticks = kernel_timeval_to_jiffies(&p->stime) - s_ticks;

	if (u_ticks) {
		do_it_virt(p, u_ticks);
		do_it_prof(p);
	}

	if (s_ticks)
		do_it_prof(p);

	update_kstatpercpu(p, time);
}

/*
 * Called from the timer interrupt handler to charge one tick to the current 
 * process.  user_tick is 1 if the tick is user time, 0 for system.  This
 * variant is called when we are mixing both statistical, tick based accounting
 * with more accurate accounting methods, it keeps our numbers from getting
 * off.
 */
static void update_process_times_mixed(int user_mode)
{
	struct task_struct *p, *unaccounted_task;
	int cpu = smp_processor_id();
	static unsigned long remainder = 0;
	struct kernel_stat_tick_times time;
	long unaccounted_usecs, accounted_usec_sum;

	memset(&time, 0, sizeof(time));
	if(process_timing.tick)
		process_timing.tick();
	accounted_usec_sum = kstat_percpu[cpu].accumulated_time.u_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.n_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.s_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.irq_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.softirq_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.iowait_usec;
	accounted_usec_sum += kstat_percpu[cpu].accumulated_time.idle_usec;
	unaccounted_usecs = (1000000/HZ);
	remainder += (1000000%HZ);
	while (remainder >= 1000000) {
		unaccounted_usecs++;
		remainder -= 1000000;
	}
	unaccounted_usecs -= accounted_usec_sum;
	if (unaccounted_usecs < 0)
		unaccounted_usecs = 0;

	if (unaccounted_usecs) {
		p = current;
		memset(&time, 0, sizeof(time));
		/* If this is an accounted task, add the extra to the
		 * last unaccounted task instead of blowing this tasks
		 * accounting.
		 */
		if (p->flags & PF_TIMING) {
			/*
			 * We move the idle ptr back to the unaccounted task
			 * each time we use it, the context switch code is
			 * responsible for setting it back to a real process
			 * whenever it is run, but this makes sure that if
			 * we have only a few unaccounted processes on the
			 * system that we don't end up putting all unaccounted
			 * time on those tasks even if they aren't being run
			 * currently.
			 */
			unaccounted_task = kstat_percpu[cpu].unaccounted_task;
			kstat_percpu[cpu].unaccounted_task = cpu_idle_ptr(cpu);
		} else
			unaccounted_task = p;

		if (user_mode) {
			if (task_nice(unaccounted_task) > 0)
				time.n_usec = unaccounted_usecs;
			else
				time.u_usec = unaccounted_usecs;
		} else {
			if (!(process_timing.flags & PT_FLAGS_IRQ) &&
			    local_irq_count(cpu) > 1)
				time.irq_usec = unaccounted_usecs;
			else if (!(process_timing.flags & PT_FLAGS_SOFTIRQ) &&
				 local_bh_count(cpu))
				time.softirq_usec = unaccounted_usecs;
			else
				time.s_usec = unaccounted_usecs;
		}
		update_process_time_intertick(unaccounted_task, &time);
	}
}

/*
 * Update process times based upon the traditional timer tick statistical
 * method.  No accounting is currently enabled, so no magic need be done to
 * keep accounted time and statistical time in sync.
 */
static void update_process_times_statistical(int user_mode)
{
	struct task_struct *p = current;
	struct kernel_stat_tick_times time;
	unsigned long usecs;
	static unsigned long remainder=0;
	int cpu = smp_processor_id();

	memset(&time, 0, sizeof(time));
	usecs = (1000000/HZ);
	remainder += (1000000%HZ);
	while (remainder >= 1000000) {
		usecs++;
		remainder -= 1000000;
	}
	if (user_mode) {
		if (task_nice(p) > 0)
			time.n_usec = usecs;
		else
			time.u_usec = usecs;
	} else {
		if (local_irq_count(cpu) > 1)
			time.irq_usec = usecs;
		else if (local_bh_count(cpu))
			time.softirq_usec = usecs;
		else
			time.s_usec = usecs;
	}
	update_process_time_intertick(p, &time);
}

/*
 * Figure out which method of accounting to use and call the right function.
 */
void update_process_times(int user_mode)
{
	if (process_timing.flags == 0)
		/* No process accounting is enabled, use straight statistical
		 * accounting */
		update_process_times_statistical(user_mode);
	else if (process_timing.flags == PT_FLAGS_EVERYTHING)
		process_timing.tick();
	else
		update_process_times_mixed(user_mode);
	scheduler_tick(1 /* update time stats */);
}

/*
 * Called from the timer interrupt handler to charge a couple of ticks
 * to the current process.
 */
void update_process_times_us(int user_ticks, int system_ticks)
{
	struct kernel_stat_tick_times time;
	struct task_struct *p = current;
	int cpu = smp_processor_id();

	/*
	 * Making a terribe hash out of accounting here. XXX
	 * Ticks are fractional these days, but oh well...
	 */
	memset(&time, 0, sizeof(struct kernel_stat_tick_times));
	if (task_nice(p) > 0)
		time.n_usec = user_ticks*(1000000/HZ);
	else
		time.u_usec = user_ticks*(1000000/HZ);
	time.s_usec = system_ticks*(1000000/HZ);
	update_one_process(p, &time, cpu);

	update_kstatpercpu(p, &time);
	scheduler_tick(1);
}

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
static unsigned long count_active_tasks(void)
{
	return (nr_running() + nr_uninterruptible()) * FIXED_1;
}

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 */
unsigned long avenrun[3];

static inline void calc_load(unsigned long ticks)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	count -= ticks;
	while (count < 0) {
		count += LOAD_FREQ;
		active_tasks = count_active_tasks();
		CALC_LOAD(avenrun[0], EXP_1, active_tasks);
		CALC_LOAD(avenrun[1], EXP_5, active_tasks);
		CALC_LOAD(avenrun[2], EXP_15, active_tasks);
	}
}

/* jiffies at the most recent update of wall time */
unsigned long wall_jiffies;


void do_timer(struct pt_regs *regs)
{
	(*(unsigned long *)&jiffies)++;
#ifndef CONFIG_SMP
	/* SMP process accounting uses the local APIC timer */

	update_process_times(user_mode(regs));
#endif
	mark_bh(TIMER_BH);
	if (TQ_ACTIVE(tq_timer))
		mark_bh(TQUEUE_BH);
}

void do_timer_ticks(int ticks)
{
	(*(unsigned long *)&jiffies) += ticks;
	mark_bh(TIMER_BH);
	if (TQ_ACTIVE(tq_timer))
		mark_bh(TQUEUE_BH);
}

#if !defined(__alpha__) && !defined(__ia64__)

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage unsigned long sys_alarm(unsigned int seconds)
{
	struct itimerval it_new, it_old;
	unsigned int oldalarm;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	do_setitimer(ITIMER_REAL, &it_new, &it_old);
	oldalarm = it_old.it_value.tv_sec;
	/* ehhh.. We can't return 0 if we have an alarm pending.. */
	/* And we'd better return too much than too little anyway */
	if (it_old.it_value.tv_usec)
		oldalarm++;
	return oldalarm;
}

#endif

#ifndef __alpha__

/*
 * The Alpha uses getxpid, getxuid, and getxgid instead.  Maybe this
 * should be moved into arch/i386 instead?
 */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
asmlinkage long sys_getpid(void)
{
	return current->tgid;
}

/*
 * Accessing ->group_leader->real_parent is not SMP-safe, it could
 * change from under us. However, rather than getting any lock
 * we can use an optimistic algorithm: get the parent
 * pid, and go back and check that the parent is still
 * the same. If it has changed (which is extremely unlikely
 * indeed), we just try again..
 *
 * NOTE! This depends on the fact that even if we _do_
 * get an old value of "parent", we can happily dereference
 * the pointer (it was and remains a dereferencable kernel pointer
 * no matter what): we just can't necessarily trust the result
 * until we know that the parent pointer is valid.
 *
 * NOTE2: ->group_leader never changes from under us.
 */
asmlinkage long sys_getppid(void)
{
	int pid;
	struct task_struct *me = current;
	struct task_struct *parent;

	parent = me->group_leader->real_parent;
	for (;;) {
		pid = parent->tgid;
#if CONFIG_SMP
{
		struct task_struct *old = parent;

		/*
		 * Make sure we read the pid before re-reading the
		 * parent pointer:
		 */
		rmb();
		parent = me->group_leader->real_parent;
		if (old != parent)
			continue;
}
#endif
		break;
	}
	return pid;
}

asmlinkage long sys_getuid(void)
{
	/* Only we change this so SMP safe */
	return current->uid;
}

asmlinkage long sys_geteuid(void)
{
	/* Only we change this so SMP safe */
	return current->euid;
}

asmlinkage long sys_getgid(void)
{
	/* Only we change this so SMP safe */
	return current->gid;
}

asmlinkage long sys_getegid(void)
{
	/* Only we change this so SMP safe */
	return  current->egid;
}

#endif

static void process_timeout(unsigned long __data)
{
	wake_up_process((task_t *)__data);
}

/**
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 *
 * Make the current task sleep until @timeout jiffies have
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout jiffies are guaranteed to
 * pass before the routine returns. The routine will return 0
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task. In this case the remaining time
 * in jiffies will be returned, or 0 if the timer expired in time
 *
 * The current task state is guaranteed to be TASK_RUNNING when this 
 * routine returns.
 *
 * Specifying a @timeout value of %MAX_SCHEDULE_TIMEOUT will schedule
 * the CPU away without a bound on the timeout. In this case the return
 * value will be %MAX_SCHEDULE_TIMEOUT.
 *
 * In all cases the return value is guaranteed to be non-negative.
 */
signed long schedule_timeout(signed long timeout)
{
	struct timer_list timer;
	unsigned long expire;

	switch (timeout)
	{
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0)
		{
			printk(KERN_ERR "schedule_timeout: wrong timeout "
			       "value %lx from %p\n", timeout,
			       __builtin_return_address(0));
			current->state = TASK_RUNNING;
			goto out;
		}
	}

	expire = timeout + jiffies;

	init_timer(&timer);
	timer.expires = expire;
	timer.data = (unsigned long) current;
	timer.function = process_timeout;

	add_timer(&timer);
	schedule();
	del_timer_sync(&timer);

	timeout = expire - jiffies;

 out:
	return timeout < 0 ? 0 : timeout;
}

/* Thread ID - the internal kernel "pid" */
asmlinkage long sys_gettid(void)
{
	return current->pid;
}

asmlinkage long sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
	struct timespec t;
	unsigned long expire;

	if(copy_from_user(&t, rqtp, sizeof(struct timespec)))
		return -EFAULT;

	if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0 || t.tv_sec < 0)
		return -EINVAL;


	if (t.tv_sec == 0 && t.tv_nsec <= 2000000L &&
	    current->policy != SCHED_NORMAL)
	{
		/*
		 * Short delay requests up to 2 ms will be handled with
		 * high precision by a busy wait for all real-time processes.
		 *
		 * Its important on SMP not to do this holding locks.
		 */
		udelay((t.tv_nsec + 999) / 1000);
		return 0;
	}

	expire = timespec_to_jiffies(&t) + (t.tv_sec || t.tv_nsec);

	current->state = TASK_INTERRUPTIBLE;
	expire = schedule_timeout(expire);

	if (expire) {
		if (rmtp) {
			jiffies_to_timespec(expire, &t);
			if (copy_to_user(rmtp, &t, sizeof(struct timespec)))
				return -EFAULT;
		}
		return -EINTR;
	}
	return 0;
}



static inline void update_times(void)
{
	unsigned long ticks;

	/*
	 * update_times() is run from the raw timer_bh handler so we
	 * just know that the irqs are locally enabled and so we don't
	 * need to save/restore the flags of the local CPU here. -arca
	 */
	br_write_lock_irq(BR_XTIME_LOCK);
	vxtime_lock();

	ticks = jiffies - wall_jiffies;
	if (ticks) {
		wall_jiffies += ticks;
		update_wall_time(ticks);
	}
	vxtime_unlock();
	br_write_unlock_irq(BR_XTIME_LOCK);
	calc_load(ticks);
}

void timer_bh(void)
{
	int i;

	update_times();

	for (i = 0; i < smp_num_cpus; i++) {
		tvec_base_t *base = tvec_bases + i;
		if (time_after_eq(jiffies, base->timer_jiffies))
			__run_timers(base);
	}
}

/* dummy variable - keep it around for compatibility */
spinlock_t timerlist_lock = SPIN_LOCK_UNLOCKED;

void __init init_timervecs(void)
{
	int i, j;

	for (i = 0; i < NR_CPUS; i++) {
		tvec_base_t *base = tvec_bases + i;

		spin_lock_init(&base->lock);
		base->magic = TIMER_MAGIC;
		for (j = 0; j < TVN_SIZE; j++) {
			INIT_LIST_HEAD(base->tv5.vec + j);
			INIT_LIST_HEAD(base->tv4.vec + j);
			INIT_LIST_HEAD(base->tv3.vec + j);
			INIT_LIST_HEAD(base->tv2.vec + j);
		}
		for (j = 0; j < TVR_SIZE; j++)
			INIT_LIST_HEAD(base->tv1.vec + j);

		base->timer_jiffies = jiffies;
	}
}

