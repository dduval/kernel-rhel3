#ifndef _LINUX_DISKDUMPLIB_H
#define _LINUX_DISKDUMPLIB_H

#include <linux/interrupt.h>
#include <linux/timer.h>

void diskdump_lib_init(void);
void diskdump_lib_exit(void);
void diskdump_update(void);

void _diskdump_add_timer(struct timer_list *);
int _diskdump_del_timer(struct timer_list *);
int _diskdump_mod_timer(struct timer_list *, unsigned long);
void _diskdump_tasklet_schedule(struct tasklet_struct *);
int _diskdump_schedule_task(struct tq_struct *);

static inline void diskdump_add_timer(struct timer_list *timer)
{
	if (crashdump_mode())
		_diskdump_add_timer(timer);
	else
		add_timer(timer);
}

static inline int diskdump_del_timer(struct timer_list *timer)
{
	if (crashdump_mode())
		return _diskdump_del_timer(timer);
	else
		return del_timer(timer);
}

static inline int diskdump_mod_timer(struct timer_list *timer, unsigned long expires)
{
	if (crashdump_mode())
		return _diskdump_mod_timer(timer, expires);
	else
		return mod_timer(timer, expires);
}

static inline void diskdump_tasklet_schedule(struct tasklet_struct *tasklet)
{
	if (crashdump_mode())
		return _diskdump_tasklet_schedule(tasklet);
	else
		return tasklet_schedule(tasklet);
}

static inline int diskdump_schedule_task(struct tq_struct *task)
{
	if (crashdump_mode())
		return _diskdump_schedule_task(task);
	else
		return schedule_task(task);
}

#endif /* _LINUX_DISKDUMPLIB_H */
