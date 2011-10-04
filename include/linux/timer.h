#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/config.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct tvec_t_base_s;

struct timer_list {
	struct list_head entry;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);

	unsigned long magic;
	unsigned long lock;
	struct tvec_t_base_s *base;
};

#define TIMER_INITIALIZER(_function, _expires, _data) {		\
		.function = (_function),			\
		.expires = (_expires),				\
		.data = (_data),				\
		.lock = 0,					\
		.base = NULL,					\
	}

/***
 * init_timer - initialize a timer.
 * @timer: the timer to be initialized
 *
 * init_timer() must be done to a timer prior calling *any* of the
 * other timer functions.
 */
static inline void init_timer(struct timer_list * timer)
{
	timer->magic = 0;
	timer->lock = 0;
	timer->base = NULL;
}

extern int timer_pending(struct timer_list * timer);
extern void add_timer(struct timer_list * timer);
extern int del_timer(struct timer_list * timer);
extern int mod_timer(struct timer_list *timer, unsigned long expires);
  
#ifdef CONFIG_SMP
  extern int del_timer_sync(struct timer_list * timer);
#else
# define del_timer_sync(t) del_timer(t)
#endif

extern void it_real_fn(unsigned long);

#define RATE_LIMIT(interval)					\
({								\
	static unsigned long expires;				\
	int ok = time_after(jiffies, expires);			\
	if (ok)							\
		expires = jiffies + (interval);			\
	ok;							\
})

#ifdef CONFIG_NO_IDLE_HZ
extern unsigned long next_timer_event(void);
#endif

#endif
