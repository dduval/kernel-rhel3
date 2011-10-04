/*
 * fileset.c - handle set of files we're supposed to monitor
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
 * Written by okir@suse.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sys.h>
#include <linux/miscdevice.h>
#include <linux/audit.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

#include "audit-private.h"

#undef DEBUG_FILESET
#ifdef DEBUG_FILTER
# define DEBUG_FILESET
#endif

struct aud_file_object {
	struct list_head	link;
	atomic_t		refcnt;
	unsigned int		seq;
	char *			name;
	struct vfsmount *	vfsmnt;
	struct dentry *		dentry;
};

static LIST_HEAD(audit_fileset);
static DECLARE_MUTEX(fileset_lock);
static struct task_struct *	lock_holder;

static void			audit_fileset_invalidate(void);

/*
 * Lock the fileset
 */
int
audit_fileset_lock(void)
{
	if (lock_holder == current)
		return 0;
	down(&fileset_lock);
	lock_holder = current;
	return 1;
}

void
audit_fileset_unlock(int invalidate)
{
	if (lock_holder != current) {
		if (!invalidate)
			return;
		(void)audit_fileset_lock();
	}
	if (invalidate)
		audit_fileset_invalidate();
	lock_holder = NULL;
	up(&fileset_lock);
}

struct aud_file_object *
audit_fileset_add(char *path)
{
	struct aud_file_object	*obj;

	obj = (struct aud_file_object *) kmalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL)
		return NULL;

	memset(obj, 0, sizeof(*obj));
	INIT_LIST_HEAD(&obj->link);
	atomic_set(&obj->refcnt, 1);
	obj->name = path;

	list_add(&obj->link, &audit_fileset);

	return obj;
}

void
audit_fileset_release(struct aud_file_object *obj)
{
	if (!atomic_dec_and_test(&obj->refcnt))
		return;
	list_del(&obj->link);
	if (obj->dentry)
		dput(obj->dentry);
	if (obj->vfsmnt)
		mntput(obj->vfsmnt);
	kfree(obj->name);
	kfree(obj);
}

int
audit_fileset_match(struct aud_file_object *obj, struct sysarg_data *tgt)
{
	struct dentry	*dp = tgt->at_path.dentry;
	const char	*path = tgt->at_path.name;
	int		need_to_unlock, match = 0;

#ifdef DEBUG_FILESET
	DPRINTF("obj=%s, path=%s, dp=%p\n", obj->name, path, dp);
#endif
	need_to_unlock = audit_fileset_lock();
	if (dp == NULL)
		return 1;
	if (obj->dentry == NULL) {
		struct nameidata	nd;

		memset(&nd, 0, sizeof(nd));
		if (!path_init(obj->name, LOOKUP_POSITIVE, &nd))
			goto out;
		if (path_walk(obj->name, &nd) < 0)
			goto out;
		obj->vfsmnt = mntget(nd.mnt);
		obj->dentry = dget(nd.dentry);
		path_release(&nd);
	}

#ifdef DEBUG_FILESET
	DPRINTF("obj->dentry=%p, obj->inode=%p, dp->inode=%p\n",
			obj->dentry,
			obj->dentry->d_inode,
			dp->d_inode);
#endif

	/* See if the name matches */
	if (path && !strncmp(obj->name, path, strnlen(obj->name, PATH_MAX))) {
		match = 1;
		goto out;
	} else
	if (obj->dentry->d_inode == dp->d_inode) {
		/* This is a hard link. Replace the name we log
		 * with the real name */

#ifdef DEBUG_FILESET
		DPRINTF("is a hardlink, replacing %s -> %s\n", path, obj->name);
#endif

		/* We're lucky - we know that the path buffer
		 * has a size of PATH_MAX */
		strncpy(tgt->at_path.name, obj->name, PATH_MAX-1);
		tgt->at_path.name[PATH_MAX-1] = '\0';
		tgt->at_path.len = strlen(tgt->at_path.name);

		match = 1;
		goto out;
	}

	/* See if the name of any parent directory
	 * matches the filter entry. */
       	while (!match && dp && dp->d_parent != dp) {
		dp = dp->d_parent;
		match = (obj->dentry == dp);
	}

out:	if (need_to_unlock && !match)
		audit_fileset_unlock(0);
	return match;
}

void
audit_fileset_invalidate(void)
{
	struct aud_file_object	*obj;
	struct list_head	*pos;

	list_for_each(pos, &audit_fileset) {
		obj = list_entry(pos, struct aud_file_object, link);
		if (obj->dentry)
			dput(obj->dentry);
		obj->dentry = NULL;
		if (obj->vfsmnt)
			mntput(obj->vfsmnt);
		obj->vfsmnt = NULL;
	}
}

