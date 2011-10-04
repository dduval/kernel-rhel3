/* -*- c -*- ------------------------------------------------------------- *
 *   
 * linux/fs/autofs/autofs_i.h
 *
 *   Copyright 1997-1998 Transmeta Corporation - All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/* Internal header file for autofs */

#include <linux/auto_fs4.h>
#include <linux/list.h>

/* This is the range of ioctl() numbers we claim as ours */
#define AUTOFS_IOC_FIRST     AUTOFS_IOC_READY
#define AUTOFS_IOC_COUNT     32

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <asm/uaccess.h>

/* #define DEBUG */

#ifdef DEBUG
#define DPRINTK(D) do{ printk("pid %d: ", current->pid); printk D; } while(0)
#else
#define DPRINTK(D) do {} while(0)
#endif

#define AUTOFS_SUPER_MAGIC 0x0187

/* Unified info structure.  This is pointed to by both the dentry and
   inode structures.  Each file in the filesystem has an instance of this
   structure.  It holds a reference to the dentry, so dentries are never
   flushed while the file exists.  All name lookups are dealt with at the
   dentry level, although the filesystem can interfere in the validation
   process.  Readdir is implemented by traversing the dentry lists. */
struct autofs_info {
	struct dentry	*dentry;
	struct inode	*inode;

	int		flags;

	struct autofs_sb_info *sbi;
	unsigned long last_used;

	mode_t	mode;
	size_t	size;

	void (*free)(struct autofs_info *);
	union {
		const char *symlink;
	} u;
};

#define AUTOFS_INF_EXPIRING	(1<<0) /* dentry is in the process of expiring */

struct autofs_wait_queue {
	wait_queue_head_t queue;
	struct autofs_wait_queue *next;
	autofs_wqt_t wait_queue_token;
	/* We use the following to see what we are waiting for */
	int hash;
	int len;
	char *name;
	/* This is for status reporting upon return */
	int status;
	atomic_t wait_ctr;
};

#define AUTOFS_SBI_MAGIC 0x6d4a556d

struct autofs_sb_info {
	u32 magic;
	struct dentry *root;
	struct file *pipe;
	pid_t oz_pgrp;
	int catatonic;
	int version;
	int sub_version;
	unsigned long exp_timeout;
	int reghost_enabled;
	int needs_reghost;
	struct semaphore wq_sem;
	spinlock_t fs_lock;
	struct super_block *sb;
	struct autofs_wait_queue *queues; /* Wait queue pointer */
};

static inline struct autofs_sb_info *autofs4_sbi(struct super_block *sb)
{
	return (struct autofs_sb_info *)(sb->u.generic_sbp);
}

static inline struct autofs_info *autofs4_dentry_ino(struct dentry *dentry)
{
	return (struct autofs_info *)(dentry->d_fsdata);
}

/* autofs4_oz_mode(): do we see the man behind the curtain?  (The
   processes which do manipulations for us in user space sees the raw
   filesystem without "magic".) */

static inline int autofs4_oz_mode(struct autofs_sb_info *sbi) {
	return sbi->catatonic || current->pgrp == sbi->oz_pgrp;
}

/* Does a dentry have some pending activity? */
static inline int autofs4_ispending(struct dentry *dentry)
{
	struct autofs_info *inf = autofs4_dentry_ino(dentry);
	int pending = 0;

	if (dentry->d_flags & DCACHE_AUTOFS_PENDING)
		return 1;

	if (inf) {
		spin_lock(&inf->sbi->fs_lock);
		pending = inf->flags & AUTOFS_INF_EXPIRING;
		spin_unlock(&inf->sbi->fs_lock);
	}

	return pending;
}

static inline void autofs4_copy_atime(struct file *src, struct file *dst)
{
	dst->f_dentry->d_inode->i_atime = src->f_dentry->d_inode->i_atime;
	return;
}

struct inode *autofs4_get_inode(struct super_block *, struct autofs_info *);
struct autofs_info *autofs4_init_inf(struct autofs_sb_info *, mode_t mode);
void autofs4_free_ino(struct autofs_info *);

/* Expiration */
int is_autofs4_dentry(struct dentry *);
int autofs4_expire_run(struct super_block *, struct vfsmount *,
			struct autofs_sb_info *, struct autofs_packet_expire *);
int autofs4_expire_multi(struct super_block *, struct vfsmount *,
			struct autofs_sb_info *, int *);

/* Operations structures */

extern struct inode_operations autofs4_symlink_inode_operations;
extern struct inode_operations autofs4_dir_inode_operations;
extern struct inode_operations autofs4_root_inode_operations;
extern struct file_operations autofs4_dir_operations;
extern struct file_operations autofs4_root_operations;

/* Initializing function */

struct super_block *autofs4_read_super(struct super_block *, void *,int);
struct autofs_info *autofs4_init_ino(struct autofs_info *, struct autofs_sb_info *sbi, mode_t mode);

/* Queue management functions */

enum autofs_notify
{
	NFY_NONE,
	NFY_MOUNT,
	NFY_EXPIRE
};

int autofs4_wait(struct autofs_sb_info *,struct dentry *, enum autofs_notify);
int autofs4_wait_release(struct autofs_sb_info *,autofs_wqt_t,int);
void autofs4_catatonic_mode(struct autofs_sb_info *);

static inline int autofs4_follow_mount(struct vfsmount **mnt, struct dentry **dentry)
{
	int res = 0;

	while (d_mountpoint(*dentry)) {
		int followed = follow_down(mnt, dentry);
		if (!followed)
			break;
		res = 1;
	}
	return res;
}

#if !defined(REDHAT_KERNEL) || LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
/**
 * list_for_each_entry  -       iterate over list of given type
 * @pos:        the type * to use as a loop counter.
 * @head:       the head for your list.
 * @member:     the name of the list_struct within the struct.
 */
#define list_for_each_entry(pos, head, member)                          \
        for (pos = list_entry((head)->next, typeof(*pos), member),      \
                     prefetch(pos->member.next);                        \
             &pos->member != (head);                                    \
             pos = list_entry(pos->member.next, typeof(*pos), member),  \
                     prefetch(pos->member.next))

#endif

static inline int simple_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static inline int simple_empty_nolock(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	list_for_each_entry(child, &dentry->d_subdirs, d_child)
		if (simple_positive(child))
			goto out;
	ret = 1;
out:
	return ret;
}

static inline int simple_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	spin_lock(&dcache_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_child)
		if (simple_positive(child))
			goto out;
	ret = 1;
out:
	spin_unlock(&dcache_lock);
	return ret;
}

