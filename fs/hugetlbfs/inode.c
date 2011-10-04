/*
 * hugetlbpage-backed filesystem.  Based on ramfs.
 *
 * William Irwin, 2002
 *
 * Copyright (C) 2002 Linus Torvalds.
 * Backported from 2.5.48 11/19/2002 Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/module.h>
#include <linux/personality.h>
#include <asm/current.h>
#include <linux/sched.h>		/* remove ASAP */
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/hugetlb.h>
#include <linux/quotaops.h>
#include <linux/dnotify.h>

#include <asm/uaccess.h>

#ifndef HUGE_TASK_SIZE
#define HUGE_TASK_SIZE TASK_SIZE
#endif

extern struct list_head inode_unused;

/* some random number */
#define HUGETLBFS_MAGIC	0x958458f6

static struct super_operations hugetlbfs_ops;
static struct address_space_operations hugetlbfs_aops;
struct file_operations hugetlbfs_file_operations;
struct file_operations hugetlbfs_dir_operations;
static struct inode_operations hugetlbfs_dir_inode_operations;

static inline int hugetlbfs_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static int hugetlbfs_empty(struct dentry *dentry)
{
	struct list_head *list;

	spin_lock (&dcache_lock);
	list = dentry->d_subdirs.next;

	while (list != &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);

		if (hugetlbfs_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
		list = list->next;
	}
	spin_unlock(&dcache_lock);
	return 1;
}

int hugetlbfs_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}

static int hugetlbfs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = HUGETLBFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = 255;
	return 0;
}

static int hugetlbfs_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	int error = -ENOTEMPTY;

	if (hugetlbfs_empty(new_dentry)) {
		struct inode *inode = new_dentry->d_inode;
		if (inode) {
			inode->i_nlink--;
			dput(new_dentry);
		}
		error = 0;
	}
	return error;
}

static int hugetlbfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int retval = -ENOTEMPTY;

	if (hugetlbfs_empty(dentry)) {
		struct inode *inode = dentry->d_inode;

		inode->i_nlink--;
		dput(dentry);			/* Undo the count from "create" - this does all the work */
		retval = 0;
	}
	return retval;
}

#define hugetlbfs_rmdir hugetlbfs_unlink

static int hugetlbfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	inode->i_nlink++;
	atomic_inc(&inode->i_count);
	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}

static struct dentry *hugetlbfs_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode =file->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	loff_t len;
	int ret;

	if (vma->vm_start & ~HPAGE_MASK)
		return -EINVAL;
	if (vma->vm_end & ~HPAGE_MASK)
		return -EINVAL;
	if (vma->vm_end - vma->vm_start < HPAGE_SIZE)
		return -EINVAL;
	
	down(&inode->i_sem);

	UPDATE_ATIME(inode);
	vma->vm_flags |= VM_HUGETLB | VM_RESERVED;
	vma->vm_ops = &hugetlb_vm_ops;
	ret = hugetlb_prefault(mapping, vma);

	len = (loff_t)(vma->vm_end - vma->vm_start) +
			((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	if (!ret && inode->i_size < len)
		inode->i_size = len;
	up(&inode->i_sem);

	return ret;
}

/*
 * Called under down_write(mmap_sem), page_table_lock is not held
 */

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
#else
static unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int full_search = 1;

	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (len > HUGE_TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = ALIGN(addr, HPAGE_SIZE);
		vma = find_vma(mm, addr);
		if (HUGE_TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	addr = ALIGN(mm->free_area_cache, HPAGE_SIZE);

repeat_loop:
	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (HUGE_TASK_SIZE - len < addr) {
			if (full_search) {
				full_search = 0;
				addr = 0;
				goto repeat_loop;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, HPAGE_SIZE);
	}
}
#endif

/*
 * Read a page. Again trivial. If it didn't already exist
 * in the page cache, it is zero-filled.
 */
static int hugetlbfs_readpage(struct file *file, struct page * page)
{
	unlock_page(page);
	return -EINVAL;
}

static int hugetlbfs_prepare_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	return -EINVAL;
}

static int hugetlbfs_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	return -EINVAL;
}

void truncate_partial_hugepage(struct page *page, unsigned partial)
{
	int i;
	const unsigned piece = partial & (PAGE_SIZE - 1);
	const unsigned tailstart = PAGE_SIZE - piece;
	const unsigned whole_pages = partial / PAGE_SIZE;
	const unsigned last_page_offset = HPAGE_SIZE/PAGE_SIZE - whole_pages;

	for (i = HPAGE_SIZE/PAGE_SIZE - 1; i >= last_page_offset; ++i)
		memclear_highpage_flush(&page[i], 0, PAGE_SIZE);

	if (!piece)
		return;

	memclear_highpage_flush(&page[last_page_offset - 1], tailstart, piece);
}

void truncate_huge_page(struct address_space *mapping, struct page *page)
{
	BUG_ON(page->mapping != mapping);

	ClearPageDirty(page);
	ClearPageUptodate(page);
	remove_inode_page(page);
	set_page_count(page, 1);

	BUG_ON(page->mapping);

	huge_page_release(page);
}

void truncate_hugepages(struct inode *inode, struct address_space *mapping, loff_t lstart)
{
	unsigned long  start = (lstart + HPAGE_SIZE - 1) >> HPAGE_SHIFT;
	unsigned partial = lstart & (HPAGE_SIZE - 1);
	unsigned long next;
	unsigned long max_idx;
	struct page *page;

	max_idx = inode->i_size >> HPAGE_SHIFT;
	next = start;
	while (next < max_idx) {
		page = find_lock_page(mapping, next);
		next++;
		if (!page)
			continue;
		truncate_huge_page(mapping, page);
		unlock_page(page);
	}

	if (partial) {
		struct page *page = find_lock_page(mapping, start - 1);
		if (page) {
			truncate_partial_hugepage(page, partial);
			unlock_page(page);
			huge_page_release(page);
		}
	}
}

static void hugetlbfs_drop_inode(struct inode *inode)
{
	if (inode->i_data.nrpages)
		truncate_hugepages(inode, &inode->i_data, 0);
}

static void hugetlb_vmtruncate_list(struct vm_area_struct *mpnt, unsigned long pgoff)
{

	do {
		unsigned long start = mpnt->vm_start;
		unsigned long end = mpnt->vm_end;
		unsigned long size = end - start;
		unsigned long diff;
		if (mpnt->vm_pgoff >= pgoff) {
			zap_hugepage_range(mpnt, start, size);
			continue;
		}
		size >>= PAGE_SHIFT;
		diff = pgoff - mpnt->vm_pgoff;
		if (diff >= size)
			continue;
		start += diff << PAGE_SHIFT;
		size = (size - diff) << PAGE_SHIFT;
		zap_hugepage_range(mpnt, start, size);
	}while ((mpnt = mpnt->vm_next_share)!= NULL);
}

static int hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	unsigned long pgoff;
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	pgoff = (offset + HPAGE_SIZE - 1) >> HPAGE_SHIFT;

	if (inode->i_size < offset)
		goto do_expand;

	inode->i_size = offset;
	spin_lock(&mapping->i_shared_lock);
	if (!mapping->i_mmap && !mapping->i_mmap_shared)
		goto out_unlock;
	if (mapping->i_mmap != NULL)
		hugetlb_vmtruncate_list(mapping->i_mmap, pgoff);
	if (mapping->i_mmap_shared != NULL)
		hugetlb_vmtruncate_list(mapping->i_mmap_shared, pgoff);

out_unlock:
	spin_unlock(&mapping->i_shared_lock);
	truncate_hugepages(inode, mapping, offset);
	return 0;

do_expand:
	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out;
	inode->i_size = offset;
	return 0;

out_sig:
	send_sig(SIGXFSZ, current, 0);
out:
	return -EFBIG;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	unsigned int ia_valid = attr->ia_valid;

	BUG_ON(!inode);

	error = inode_change_ok(inode, attr);
	if (error)
		goto out;

	if ((ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
	    (ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid))
		error = DQUOT_TRANSFER(inode, attr) ? -EDQUOT : 0;
	if (error)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		error = -EINVAL;
		if (!(attr->ia_size & ~HPAGE_MASK))
			error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			goto out;
		attr->ia_valid &= ~ATTR_SIZE;
	}
	error = inode_setattr(inode, attr);
out:
	return error;
}

struct inode *hugetlbfs_get_inode(struct super_block *sb, uid_t uid, gid_t gid,
				int mode, int dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = uid;
		inode->i_gid = gid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &hugetlbfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hugetlbfs_dir_inode_operations;
			inode->i_fop = &hugetlbfs_dir_operations;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int hugetlbfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode * inode = hugetlbfs_get_inode(dir->i_sb, current->fsuid,
						current->fsgid, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);		/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hugetlbfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	int error;

	error = hugetlbfs_mknod(dir, dentry, S_IFLNK|S_IRWXUGO, 0);
	if (!error) {
		int l = strlen(symname)+1;
		struct inode *inode = dentry->d_inode;
		error = block_symlink(inode, symname, l);
	}
	return error;
}

static struct address_space_operations hugetlbfs_aops = {
	readpage:	hugetlbfs_readpage,
	writepage:	fail_writepage,
	prepare_write:	hugetlbfs_prepare_write,
	commit_write:	hugetlbfs_commit_write
};

struct file_operations hugetlbfs_file_operations = {
	read:			generic_file_read,
	write:			generic_file_write,
	mmap:			hugetlbfs_file_mmap,
	fsync:			hugetlbfs_sync_file,
	get_unmapped_area:	hugetlb_get_unmapped_area
};

struct file_operations hugetlbfs_dir_operations = {
	open:		dcache_dir_open,
	release:	dcache_dir_close,
	llseek:		dcache_dir_lseek,
	read:		generic_read_dir,
	readdir:	dcache_readdir,
	fsync:		hugetlbfs_sync_file,
};

static struct inode_operations hugetlbfs_dir_inode_operations = {
	create:		hugetlbfs_create,
	lookup:		hugetlbfs_lookup,
	link:		hugetlbfs_link,
	unlink:		hugetlbfs_unlink,
	symlink:	hugetlbfs_symlink,
	mkdir:		hugetlbfs_mkdir,
	rmdir:		hugetlbfs_rmdir,
	mknod:		hugetlbfs_mknod,
	rename:		hugetlbfs_rename,
	setattr:	hugetlbfs_setattr,
};

static struct super_operations hugetlbfs_ops = {
	statfs:		hugetlbfs_statfs,
	put_inode:	hugetlbfs_drop_inode,
};

static int hugetlbfs_parse_options(char *options, struct hugetlbfs_config *pconfig)
{
	char *opt, *value, *rest;

	if (!options)
		return 0;
	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;

		value = strchr(opt, '=');
		if (!value || !*value)
			return -EINVAL;
		else
			*value++ = '\0';

		if (!strcmp(opt, "uid"))
			pconfig->uid = simple_strtoul(value, &value, 0);
		else if (!strcmp(opt, "gid"))
			pconfig->gid = simple_strtoul(value, &value, 0);
		else if (!strcmp(opt, "mode"))
			pconfig->mode = simple_strtoul(value, &value, 0) & 0777U;
		else 
			return -EINVAL;

		if (*value)
			return -EINVAL;
	}
	return 0;
}

static struct super_block * hugetlbfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	struct hugetlbfs_config config;

	config.uid = current->fsuid;
	config.gid = current->fsgid;
	config.mode = 0755;

	if (hugetlbfs_parse_options(data, &config))
		return NULL;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	inode = hugetlbfs_get_inode(sb, config.uid, config.gid, S_IFDIR | config.mode, 0);
	if (!inode)
		return NULL;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	return sb;
}

static DECLARE_FSTYPE(hugetlbfs_fs_type, "hugetlbfs", hugetlbfs_fill_super, FS_LITTER);

static struct vfsmount *hugetlbfs_vfsmount;

static atomic_t hugetlbfs_counter = ATOMIC_INIT(0);

struct file *hugetlb_zero_setup(size_t size)
{
	int error, n;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry, *root;
	struct qstr quick_string;
	char buf[16];

	if (!is_hugepage_mem_enough(size))
		return ERR_PTR(-ENOMEM);
	n = atomic_read(&hugetlbfs_counter);
	atomic_inc(&hugetlbfs_counter);

	root = hugetlbfs_vfsmount->mnt_root;
	snprintf(buf, 16, "%d", n);
	quick_string.name = buf;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	dentry = d_alloc(root, &quick_string);
	if (!dentry)
		return ERR_PTR(-ENOMEM);

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto out_dentry;

	error = -ENOSPC;
	inode = hugetlbfs_get_inode(root->d_sb, current->fsuid, current->fsgid,
				S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_file;

	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;
	file->f_vfsmnt = mntget(hugetlbfs_vfsmount);
	file->f_dentry = dentry;
	file->f_op = &hugetlbfs_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;
	return file;

out_file:
	put_filp(file);
out_dentry:
	dput(dentry);
	return ERR_PTR(error);
}

static int __init init_hugetlbfs_fs(void)
{
	int error;
	struct vfsmount *vfsmount;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		return error;

	vfsmount = kern_mount(&hugetlbfs_fs_type);

	if (!IS_ERR(vfsmount)) {
		printk("Hugetlbfs mounted.\n");
		hugetlbfs_vfsmount = vfsmount;
		return 0;
	}

	printk("Error in  mounting hugetlbfs.\n");
	error = PTR_ERR(vfsmount);
	return error;
}

static void __exit exit_hugetlbfs_fs(void)
{
	unregister_filesystem(&hugetlbfs_fs_type);
}

module_init(init_hugetlbfs_fs)
module_exit(exit_hugetlbfs_fs)

MODULE_LICENSE("GPL");
