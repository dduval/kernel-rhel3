/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/inode.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/locks.h>
#include <asm/bitops.h>
#include <linux/smp_lock.h>
#include "autofs_i.h"
#define __NO_VERSION__
#include <linux/module.h>

static void ino_lnkfree(struct autofs_info *ino)
{
	if (ino->u.symlink) {
		kfree(ino->u.symlink);
		ino->u.symlink = NULL;
	}
}

struct autofs_info *autofs4_init_ino(struct autofs_info *ino,
				     struct autofs_sb_info *sbi, mode_t mode)
{
	int reinit = 1;

	if (ino == NULL) {
		reinit = 0;
		ino = kmalloc(sizeof(*ino), GFP_KERNEL);
	}

	if (ino == NULL)
		return NULL;

	ino->flags = 0;
	ino->mode = mode;
	ino->inode = NULL;
	ino->dentry = NULL;
	ino->size = 0;

	ino->last_used = jiffies;

	ino->sbi = sbi;

	if (reinit && ino->free)
		(ino->free)(ino);

	memset(&ino->u, 0, sizeof(ino->u));

	ino->free = NULL;

	if (S_ISLNK(mode))
		ino->free = ino_lnkfree;

	return ino;
}

void autofs4_free_ino(struct autofs_info *ino)
{
	if (ino->dentry) {
		ino->dentry->d_fsdata = NULL;
		if (ino->dentry->d_inode)
			dput(ino->dentry);
		ino->dentry = NULL;
	}
	if (ino->free)
		(ino->free)(ino);
	kfree(ino);
}

/*
 * Deal with the infamous "Busy inodes after umount ..." message.
 *
 * Clean up the dentry tree. This happens with autofs if the user
 * space program goes away due to a SIGKILL, SIGSEGV etc.
 */
static void autofs4_force_release(struct autofs_sb_info *sbi)
{
	struct dentry *this_parent = sbi->root;
	struct list_head *next;

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct dentry *dentry = list_entry(next, struct dentry, d_child);

		/* Negative dentry - don`t care */
		if (!simple_positive(dentry)) {
			next = next->next;
			continue;
		}

		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}

		next = next->next;
		spin_unlock(&dcache_lock);

		DPRINTK(("dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name));

		dput(dentry);
		spin_lock(&dcache_lock);
	}

	if (this_parent != sbi->root) {
		struct dentry *dentry = this_parent;

		next = this_parent->d_child.next;
		this_parent = this_parent->d_parent;
		spin_unlock(&dcache_lock);
		DPRINTK(("parent dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name));
		dput(dentry);
		spin_lock(&dcache_lock);
		goto resume;
	}
	spin_unlock(&dcache_lock);

	dput(sbi->root);
	sbi->root = NULL;
	shrink_dcache_sb(sbi->sb);

	return;
}

static void autofs4_put_super(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs4_sbi(sb);

	sb->u.generic_sbp = NULL;

	if ( !sbi->catatonic )
		autofs4_catatonic_mode(sbi); /* Free wait queues, close pipe */

	/* Clean up and release dangling references */
	autofs4_force_release(sbi);

	kfree(sbi);

	DPRINTK(("autofs4: shutting down\n"));
}

static int autofs4_statfs(struct super_block *sb, struct statfs *buf);

static struct super_operations autofs4_sops = {
	put_super:	autofs4_put_super,
	statfs:		autofs4_statfs,
};

static int parse_options(char *options, int *pipefd, uid_t *uid, gid_t *gid,
			 pid_t *pgrp, int *minproto, int *maxproto)
{
	char *this_char, *value;
	
	*uid = current->uid;
	*gid = current->gid;
	*pgrp = current->pgrp;

	*minproto = AUTOFS_MIN_PROTO_VERSION;
	*maxproto = AUTOFS_MAX_PROTO_VERSION;

	*pipefd = -1;

	if ( !options ) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"fd")) {
			if (!value || !*value)
				return 1;
			*pipefd = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 1;
			*uid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 1;
			*gid = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"pgrp")) {
			if (!value || !*value)
				return 1;
			*pgrp = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"minproto")) {
			if (!value || !*value)
				return 1;
			*minproto = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"maxproto")) {
			if (!value || !*value)
				return 1;
			*maxproto = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else break;
	}
	return (*pipefd < 0);
}

static struct autofs_info *autofs4_mkroot(struct autofs_sb_info *sbi)
{
	struct autofs_info *ino;

	ino = autofs4_init_ino(NULL, sbi, S_IFDIR | 0755);
	if (!ino)
		return NULL;

	return ino;
}

struct super_block *autofs4_read_super(struct super_block *s, void *data,
				      int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct file * pipe;
	int pipefd;
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	int minproto, maxproto;

	sbi = (struct autofs_sb_info *) kmalloc(sizeof(*sbi), GFP_KERNEL);
	if ( !sbi )
		goto fail_unlock;
	DPRINTK(("autofs4: starting up, sbi = %p\n",sbi));

	memset(sbi, 0, sizeof(*sbi));

	s->u.generic_sbp = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->root = NULL;
	sbi->catatonic = 0;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = current->pgrp;
	sbi->sb = s;
	sbi->version = 0;
	sbi->sub_version = 0;
	init_MUTEX(&sbi->wq_sem);
	spin_lock_init(&sbi->fs_lock);
	sbi->queues = NULL;
	sbi->reghost_enabled = 0;
	sbi->needs_reghost = 0;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs4_sops;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	ino = autofs4_mkroot(sbi);
	root_inode = autofs4_get_inode(s, ino);
	kfree(ino);
	if (!root_inode)
		goto fail_free;

	root_inode->i_op = &autofs4_root_inode_operations;
	root_inode->i_fop = &autofs4_root_operations;
	root = d_alloc_root(root_inode);
	pipe = NULL;

	if (!root)
		goto fail_iput;

	/* Can this call block? */
	if (parse_options(data, &pipefd,
			  &root_inode->i_uid, &root_inode->i_gid,
			  &sbi->oz_pgrp,
			  &minproto, &maxproto)) {
		printk("autofs4: called with bogus options\n");
		goto fail_dput;
	}

	/* Couldn't this be tested earlier? */
	if (maxproto < AUTOFS_MIN_PROTO_VERSION ||
	    minproto > AUTOFS_MAX_PROTO_VERSION) {
		printk("autofs4: kernel does not match daemon version "
		       "daemon (%d, %d) kernel (%d, %d)\n",
			minproto, maxproto,
			AUTOFS_MIN_PROTO_VERSION, AUTOFS_MAX_PROTO_VERSION);
		goto fail_dput;
	}

	sbi->version = maxproto > AUTOFS_MAX_PROTO_VERSION ? AUTOFS_MAX_PROTO_VERSION : maxproto;
	sbi->sub_version =  AUTOFS_PROTO_SUBVERSION;

	DPRINTK(("autofs4: pipe fd = %d, pgrp = %u\n", pipefd, sbi->oz_pgrp));
	pipe = fget(pipefd);
	
	if ( !pipe ) {
		printk("autofs4: could not open pipe file descriptor\n");
		goto fail_dput;
	}
	if ( !pipe->f_op || !pipe->f_op->write )
		goto fail_fput;
	sbi->pipe = pipe;

	/*
	 * Take a reference to the root dentry so we get a chance to
	 * clean up the dentry tree on umount.
	 * See autofs4_force_release.
	 */
	sbi->root = dget(root);

	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;
	return s;
	
	/*
	 * Failure ... clean up.
	 */
fail_fput:
	printk("autofs4: pipe file descriptor does not contain proper ops\n");
	/*
	 * fput() can block, so we clear the super block first.
	 */
	fput(pipe);
	/* fall through */
fail_dput:
	/*
	 * dput() can block, so we clear the super block first.
	 */
	dput(root);
	goto fail_free;
fail_iput:
	printk("autofs4: get root dentry failed\n");
	/*
	 * iput() can block, so we clear the super block first.
	 */
	iput(root_inode);
fail_free:
	kfree(sbi);
fail_unlock:
	return NULL;
}

static int autofs4_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = AUTOFS_SUPER_MAGIC;
	buf->f_bsize = 1024;
	buf->f_namelen = NAME_MAX;
	return 0;
}

struct inode *autofs4_get_inode(struct super_block *sb,
				struct autofs_info *inf)
{
	struct inode *inode = new_inode(sb);

	if (inode == NULL)
		return NULL;

	inf->inode = inode;
	inode->i_mode = inf->mode;
	if (sb->s_root) {
		inode->i_uid = sb->s_root->d_inode->i_uid;
		inode->i_gid = sb->s_root->d_inode->i_gid;
	} else {
		inode->i_uid = 0;
		inode->i_gid = 0;
	}
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_rdev = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	if (S_ISDIR(inf->mode)) {
		inode->i_nlink = 2;
		inode->i_op = &autofs4_dir_inode_operations;
		inode->i_fop = &autofs4_dir_operations;
	} else if (S_ISLNK(inf->mode)) {
		inode->i_size = inf->size;
		inode->i_op = &autofs4_symlink_inode_operations;
	}

	return inode;
}
