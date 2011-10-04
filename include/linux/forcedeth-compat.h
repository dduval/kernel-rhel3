#ifndef __FORCEDETH_COMPAT_H__
#define __FORCEDETH_COMPAT_H__

/* For 2.6.x compatibility */
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#if HZ <= 1000 && !(1000 % HZ)
#  define MAX_MSEC_OFFSET \
	(ULONG_MAX - (1000 / HZ) + 1)
#elif HZ > 1000 && !(HZ % 1000)
#  define MAX_MSEC_OFFSET \
	(ULONG_MAX / (HZ / 1000))
#else
#  define MAX_MSEC_OFFSET \
	((ULONG_MAX - 999) / HZ)
#endif

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	if (MAX_MSEC_OFFSET < UINT_MAX && m > (unsigned int)MAX_MSEC_OFFSET)
		return MAX_JIFFY_OFFSET;
#if HZ <= 1000 && !(1000 % HZ)
	return ((unsigned long)m + (1000 / HZ) - 1) / (1000 / HZ);
#elif HZ > 1000 && !(HZ % 1000)
	return (unsigned long)m * (HZ / 1000);
#else
	return ((unsigned long)m * HZ + 999) / 1000;
#endif
}

static inline void msleep(unsigned long msecs)
{
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

#endif /* __FORCEDETH_COMPAT_H__ */
