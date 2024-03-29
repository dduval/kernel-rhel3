/*
 *  include/linux/eventpoll.h ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2003	 Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <linux/config.h>
#include <linux/types.h>

#ifdef CONFIG_EPOLL

/* Valid opcodes to issue to sys_epoll_ctl() */
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

/* Set the One Shot behaviour for the target file descriptor */
#define EPOLLONESHOT (1 << 30)

/* Set the Edge Triggered behaviour for the target file descriptor */
#define EPOLLET (1 << 31)

/*
 * On x86-64 and ia64 make the 64bit structure have the same alignment 
 * as the 32bit structure. This makes 32bit emulation easier.
 */
#if defined(__x86_64__)
#define EPOLL_PACKED __attribute__((packed))
#else
#define EPOLL_PACKED
#endif

struct epoll_event {
	__u32 events;
	__u64 data;
} EPOLL_PACKED;

#ifdef __KERNEL__

/* Forward declarations to avoid compiler errors */
struct file;


/* Kernel space functions implementing the user space "epoll" API */
asmlinkage long sys_epoll_create(int size);
asmlinkage long sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
asmlinkage long sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
			       int timeout);

/* Used to initialize the epoll bits inside the "struct file" */
void eventpoll_init_file(struct file *file);

/* Used to release the epoll bits inside the "struct file" */
void eventpoll_release_file(struct file *file);

/*
 * This is called from inside fs/file_table.c:__fput() to unlink files
 * from the eventpoll interface. We need to have this facility to cleanup
 * correctly files that are closed without being removed from the eventpoll
 * interface.
 */
static inline void eventpoll_release(struct file *file)
{

	/*
	 * Fast check to avoid the get/release of the semaphore. Since
	 * we're doing this outside the semaphore lock, it might return
	 * false negatives, but we don't care. It'll help in 99.99% of cases
	 * to avoid the semaphore lock. False positives simply cannot happen
	 * because the file in on the way to be removed and nobody ( but
	 * eventpoll ) has still a reference to this file.
	 */
	if (likely(list_empty(&file->f_ep_links)))
		return;

	/*
	 * The file is being closed while it is still linked to an epoll
	 * descriptor. We need to handle this by correctly unlinking it
	 * from its containers.
	 */
	eventpoll_release_file(file);
}


#endif /* #ifdef __KERNEL__ */

#else

#define eventpoll_init_file(file)  do { ; } while(0)
#define eventpoll_release(file)    do { ; } while(0)

#endif /* CONFIG_EPOLL */

#endif /* #ifndef _LINUX_EVENTPOLL_H */
