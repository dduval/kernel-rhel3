/**
 * @file oprofilefs.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 *
 * A simple filesystem for configuration and
 * access of oprofile.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/oprofile.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "oprof.h"

#define OPROFILEFS_MAGIC 0x6f70726f

static struct inode * oprofilefs_get_inode(struct super_block * sb, int mode)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}

/* copied from 2.5.46 fs/libfs.c:simple_statfs() */
static int oprofilefs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = sb->s_magic;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

static struct super_operations s_ops = {
	.statfs		= oprofilefs_statfs,
/* FIXME should I have something for .delete_inode ??? */
/*	.drop_inode 	= generic_delete_inode, */
};


ssize_t oprofilefs_str_to_user(char const * str, char * buf, size_t count, loff_t * offset)
{
	size_t len = strlen(str);
	loff_t pos = *offset;

	if (!count)
		return 0;

	if (pos < 0 || pos > len)
		return 0;

	if (count > len - pos)
		count = len - pos;

	if (copy_to_user(buf, str + pos, count))
		return -EFAULT;

	*offset = pos + count;

	return count;
}


#define TMPBUFSIZE 50

ssize_t oprofilefs_ulong_to_user(unsigned long * val, char * buf, size_t count, loff_t * offset)
{
	char tmpbuf[TMPBUFSIZE];
	size_t maxlen;
	loff_t pos = *offset;

	if (!count)
		return 0;

	maxlen = snprintf(tmpbuf, TMPBUFSIZE, "%lu\n", *val);

	if (pos < 0 || pos > maxlen)
		return 0;

	if (count > maxlen - pos)
		count = maxlen - pos;

	if (copy_to_user(buf, tmpbuf + pos, count))
		return -EFAULT;

	*offset = pos + count;

	return count;
}


int oprofilefs_ulong_from_user(unsigned long * val, char const * buf, size_t count)
{
	char tmpbuf[TMPBUFSIZE];

	if (!count)
		return 0;

	if (count > TMPBUFSIZE - 1)
		return -EINVAL;

	memset(tmpbuf, 0x0, TMPBUFSIZE);

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;

	*val = simple_strtoul(tmpbuf, NULL, 0);

	return 0;
}


static ssize_t ulong_read_file(struct file * file, char * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(file->private_data, buf, count, offset);
}


static ssize_t ulong_write_file(struct file * file, char const * buf, size_t count, loff_t * offset)
{
	unsigned long * value = file->private_data;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(value, buf, count);

	if (retval)
		return retval;
	return count;
}


static int default_open(struct inode * inode, struct file * filp)
{
	if (inode->u.generic_ip)
		filp->private_data = inode->u.generic_ip;
	return 0;
}


static struct file_operations ulong_fops = {
	.read		= ulong_read_file,
	.write		= ulong_write_file,
	.open		= default_open,
};


static struct file_operations ulong_ro_fops = {
	.read		= ulong_read_file,
	.open		= default_open,
};


static struct dentry * __oprofilefs_create_file(struct super_block * sb,
	struct dentry * root, char const * name, struct file_operations * fops)
{
	struct dentry * dentry;
	struct inode * inode;
	struct qstr qname;
	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(root, &qname);
	if (!dentry)
		return 0;
	inode = oprofilefs_get_inode(sb, S_IFREG | 0644);
	if (!inode) {
		dput(dentry);
		return 0;
	}
	inode->i_fop = fops;
	d_add(dentry, inode);
	return dentry;
}


int oprofilefs_create_ulong(struct super_block * sb, struct dentry * root,
	char const * name, unsigned long * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name, &ulong_fops);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}


int oprofilefs_create_ro_ulong(struct super_block * sb, struct dentry * root,
	char const * name, unsigned long * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name, &ulong_ro_fops);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}


static ssize_t atomic_read_file(struct file * file, char * buf, size_t count, loff_t * offset)
{
	atomic_t * aval = file->private_data;
	unsigned long val = atomic_read(aval);
	return oprofilefs_ulong_to_user(&val, buf, count, offset);
}
 

static struct file_operations atomic_ro_fops = {
	.read		= atomic_read_file,
	.open		= default_open,
};
 

int oprofilefs_create_ro_atomic(struct super_block * sb, struct dentry * root,
	char const * name, atomic_t * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name, &atomic_ro_fops);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}

 
int oprofilefs_create_file(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops)
{
	if (!__oprofilefs_create_file(sb, root, name, fops))
		return -EFAULT;
	return 0;
}

/* copied from linux 2.5.46 fs/libfs.c:simple_lookup() */
static struct dentry *oprofilefs_lookup(struct inode *dir,
					struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}


/* copied from linux 2.5.46 fs/libfs.c:generic_read_dir() */
static ssize_t oprofilefs_read_dir(struct file *filp, char *buf,
				   size_t siz, loff_t *ppos)
{
	return -EISDIR;
}

/* copied from linux 2.5.46 fs/libfs.c:dt_type() */
/* Relationship between i_mode and the DT_xxx types */
static inline unsigned char dt_type(struct inode *inode)
{
	return (inode->i_mode >> 12) & 15;
}

/* adapted from linux 2.5.46 fs/libfs.c:dcache_readdir() */
/*
 * Directory is locked and all positive dentries in it are safe, since
 * for ramfs-type trees they can't go away without unlink() or rmdir(),
 * both impossible due to the lock on directory.
 */

static int oprofilefs_readdir(struct file * filp, void * dirent,
			      filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct dentry *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->d_child;
	ino_t ino;
	int i = filp->f_pos;

	switch (i) {
		case 0:
			ino = dentry->d_inode->i_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			if (filldir(dirent, "..", 2, i, 
				    dentry->d_parent->d_inode->i_ino,
				    DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		default:
			spin_lock(&dcache_lock);
			if (filp->f_pos == 2) {
				list_del(q);
				list_add(q, &dentry->d_subdirs);
			}
			for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_child);
				if (list_empty(&next->d_hash) || !next->d_inode)
					continue;

				spin_unlock(&dcache_lock);
				if (filldir(dirent, next->d_name.name, next->d_name.len, filp->f_pos, next->d_inode->i_ino, dt_type(next->d_inode)) < 0)
					return 0;
				spin_lock(&dcache_lock);
				/* next is still alive */
				list_del(q);
				list_add(q, p);
				p = q;
				filp->f_pos++;
			}
			spin_unlock(&dcache_lock);
	}
	return 0;
}

/* copied from linux 2.5.46 fs/libfs.c:simple_dir_operations */
struct file_operations oprofilefs_dir_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= oprofilefs_read_dir,
	.readdir	= oprofilefs_readdir,
};

/* copied from linux 2.5.46 fs/libfs.c:simple_dir_inode_operations */
struct inode_operations oprofilefs_dir_inode_operations = {
	.lookup		= oprofilefs_lookup,
};


struct dentry * oprofilefs_mkdir(struct super_block * sb,
	struct dentry * root, char const * name)
{
	struct dentry * dentry;
	struct inode * inode;
	struct qstr qname;
	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(root, &qname);
	if (!dentry)
		return 0;
	inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!inode) {
		dput(dentry);
		return 0;
	}
	inode->i_op = &oprofilefs_dir_inode_operations;
	inode->i_fop = &oprofilefs_dir_operations;
	d_add(dentry, inode);
	return dentry;
}


static struct super_block *oprofilefs_fill_super(struct super_block * sb,
						 void * data,
						 int silent)
{
	struct inode * root_inode;
	struct dentry * root_dentry;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = OPROFILEFS_MAGIC;
	sb->s_op = &s_ops;

	root_inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!root_inode)
		return NULL;
	root_inode->i_op = &oprofilefs_dir_inode_operations;
	root_inode->i_fop = &oprofilefs_dir_operations;
	root_dentry = d_alloc_root(root_inode);
	if (!root_dentry) {
		iput(root_inode);
		return NULL;
	}

	sb->s_root = root_dentry;

	oprofile_create_files(sb, root_dentry);

	// FIXME: verify kill_litter_super removes our dentries
	return sb;
}


#if 0
static struct super_block * oprofilefs_get_sb(struct file_system_type * fs_type,
	int flags, char * dev_name, void * data)
{
	return get_sb_single(fs_type, flags, data, oprofilefs_fill_super);
}


static struct file_system_type oprofilefs_type = {
	.owner		= THIS_MODULE,
	.name		= "oprofilefs",
	.get_sb		= oprofilefs_get_sb,
	.kill_sb	= kill_litter_super,
};
#else
static DECLARE_FSTYPE(oprofilefs_type, "oprofilefs", oprofilefs_fill_super, FS_SINGLE | FS_LITTER);
#endif


int __init oprofilefs_register(void)
{
	return register_filesystem(&oprofilefs_type);
}


void __exit oprofilefs_unregister(void)
{
	unregister_filesystem(&oprofilefs_type);
}
