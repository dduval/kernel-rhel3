/*
 * args.c
 *
 * Linux Audit Subsystem, argument handling
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

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/mman.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include <linux/audit.h>

#include "audit-private.h"

#undef DEBUG_MEMORY

#define AUDIT_MAX_ARGV	32	/* Max # of argv entries copied for execve() */

size_t			audit_scratch_vm_size = (AUDIT_MAX_SCRATCH_PAGES * PAGE_SIZE);


static int		__audit_copy_arg(struct aud_syscall_data *, struct sysarg *,
	       				struct sysarg_data *, u_int64_t);
static void		__audit_release_arg(struct sysarg_data *);
#ifdef DEBUG_MEMORY
static void *		mem_alloc(size_t, int);
static void		mem_free(void *);
#else
#define mem_alloc	kmalloc
#define mem_free	kfree
#endif

/*
 * Initialize VM info
 */
void
audit_init_vm(struct aud_process *pinfo)
{
	struct mm_struct *mm = current->mm;
	struct aud_vm_info *vmi;

	DPRINTF("called.\n");
	BUG_ON(pinfo->vm_info);

	if (mm == NULL)
		return;

	/* Allocate a VM info struct that is shared by
	 * all threads */
	do {
		vmi = kmalloc(sizeof(*vmi), GFP_KERNEL);
		if (vmi)
			break;
		schedule_timeout(HZ);
	} while (1);

	memset(vmi, 0, sizeof(*vmi));
	init_rwsem(&vmi->lock);
	atomic_set(&vmi->refcnt, 1);

	vmi->mm = current->mm;
	atomic_inc(&current->mm->mm_users);

	pinfo->vm_info = vmi;
}

void
audit_release_vm(struct aud_process *pinfo)
{
	struct aud_vm_info *vmi;

	if ((vmi = pinfo->vm_info) == NULL)
		return;

	DPRINTF("called, refcnt=%d.\n", atomic_read(&vmi->refcnt));

	/* Release scratch pages we mapped previously */
	audit_release_scratch_vm(pinfo);

	pinfo->vm_info = NULL;
	if (atomic_dec_and_test(&vmi->refcnt)) {
		mmput(vmi->mm);
		kfree(vmi);
	}
}

void
audit_copy_vm(struct aud_process *pinfo, struct aud_process *parent)
{
	struct aud_vm_info *vmi = parent->vm_info;

	pinfo->vm_info = vmi;
	if (vmi)
		atomic_inc(&vmi->refcnt);
}

/*
 * Lock/unlock VM semaphore
 */
static inline void
audit_lock_vm(struct aud_process *pinfo, int how)
{
	struct aud_vm_info *vmi;

	if (!pinfo->flags & how)
		return;

	if (how == AUD_F_VM_LOCKED_R) {
		if (pinfo->flags & AUD_F_VM_LOCKED_W)
			return;
	} else {
		if (pinfo->flags & AUD_F_VM_LOCKED_R)
			BUG();
	}

	if (!(vmi = pinfo->vm_info))
		BUG();

	if (how == AUD_F_VM_LOCKED_R)
		down_read(&vmi->lock);
	else
		down_write(&vmi->lock);

	pinfo->flags |= how;
}

static inline void
audit_unlock_vm(struct aud_process *pinfo)
{
	struct aud_vm_info *vmi;

	if ((vmi = pinfo->vm_info) != NULL) {
		if (pinfo->flags & AUD_F_VM_LOCKED_R)
			up_read(&vmi->lock);
		else if (pinfo->flags & AUD_F_VM_LOCKED_W)
			up_write(&vmi->lock);
	}

	pinfo->flags &= ~(AUD_F_VM_LOCKED_R|AUD_F_VM_LOCKED_W);
}

/*
 * Create process scratch memory
 *
 * We map some memory in the VM of the calling process, and use it to
 * copy system call arguments to.
 */
static int
audit_alloc_scratch_vm(struct aud_process *pinfo)
{
	struct mm_struct *mm = current->mm;
	unsigned long	addr;
	size_t		size;

	DPRINTF("Allocating scratch info for process %d\n", current->pid);
	BUG_ON(mm == NULL);
	BUG_ON(pinfo->vm_info == NULL);
	BUG_ON(pinfo->vm_info->mm != mm);

	/* Make this constant for now */
	size = audit_scratch_vm_size;

	/* Create a read-only anonymous mapping in the calling process' VM */
	down_write(&mm->mmap_sem);
	addr = do_mmap(NULL, 0, size, PROT_READ, MAP_ANON|MAP_PRIVATE, 0);
	up_write(&mm->mmap_sem);

	DPRINTF("Mapped scratch VM for process %d; addr=%lx\n",
		       	current->pid, addr);
	if (IS_ERR((void *) addr))
		return addr;

	pinfo->vm_area = find_vma(mm, addr);
	pinfo->vm_addr = addr;
	pinfo->vm_size = size;
	memset(pinfo->vm_page, 0, sizeof(pinfo->vm_page));
	memset(pinfo->vm_phys, 0, sizeof(pinfo->vm_phys));

	return 0;
}

/*
 * Verify that the scratch VM is still as it was when we created it
 */
static int
audit_validate_scratch_vm(struct aud_process *pinfo)
{
	struct aud_vm_info *vmi;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct	*vma;

	if (!(vmi = pinfo->vm_info))
		BUG();

	if (vmi->mm != mm) {
		/* We may have called execve(), so the mm struct
		 * has changed. Drop the entire VM info struct now,
		 * and allocate a fresh one */
		audit_unlock_vm(pinfo);
		audit_release_vm(pinfo);
		audit_init_vm(pinfo);
		return 0;
	}

	if (pinfo->vm_addr == 0) {
		audit_unlock_vm(pinfo);
		return 0;
	}

	vma = find_vma(mm, pinfo->vm_addr);
	if (vma == NULL
   	 || pinfo->vm_addr < vma->vm_start
	 || pinfo->vm_addr + pinfo->vm_size > vma->vm_end
	 || vma->vm_file
	 || (vma->vm_flags & (VM_WRITE|VM_MAYWRITE))) {
		/* The process messed with its VM in a way we cannot
		 * tolerate. We would be justified to nuke it right here,
		 * but we play nice and just re-map it */
		printk(KERN_NOTICE "Process %u (login uid=%u) changed its scratch VM\n",
				current->pid, pinfo->audit_uid);
		audit_unlock_vm(pinfo);
		audit_release_scratch_vm(pinfo);
		return 0;
	}

	return 1;
}

/*
 * Map a page of scratch memory
 */
static inline int
audit_map_scratch_page(struct aud_process *pinfo, unsigned int nr)
{
	struct aud_vm_info *vmi = pinfo->vm_info;
	struct vm_area_struct *vma;
	unsigned long	addr;
	struct page	*page;
	int		err;

	if (nr >= AUDIT_MAX_SCRATCH_PAGES)
		return -EINVAL;

	BUG_ON(vmi == NULL);
	if ((page = pinfo->vm_page[nr]) == NULL) {
		addr = pinfo->vm_addr + nr * PAGE_SIZE;
		err = get_user_pages(current, vmi->mm, addr, 1, 1, 1, &page, &vma);
		if (err < 0) {
			printk(KERN_NOTICE "%s: get_user_pages failed, err=%d\n",
					__FUNCTION__, err);
			return err;
		}

		pinfo->vm_page[nr] = page;
		pinfo->vm_phys[nr] = (unsigned long) kmap(page);
	}

	return 0;
}

/*
 * Release scratch memory
 */
void
audit_release_scratch_vm(struct aud_process *pinfo)
{
	struct aud_vm_info *vmi;
	struct mm_struct *mm;
	unsigned int	i;

	if (!(vmi = pinfo->vm_info))
		return;

	/* Release lock on scratch memory if we hold it */
	audit_unlock_vm(pinfo);

	mm = vmi->mm;
	down_write(&mm->mmap_sem);

	if (pinfo->vm_addr) {
		DPRINTF("Unmapping scratch vmi info\n");
		do_munmap(mm, pinfo->vm_addr, pinfo->vm_size, 0);

		pinfo->vm_addr = 0;
		pinfo->vm_size = 0;
	}

	for (i = 0; i < AUDIT_MAX_SCRATCH_PAGES; i++) {
		struct page	*page;

		if (!(page = pinfo->vm_page[i]))
			continue;

		kunmap(page);
		put_page(page);
		pinfo->vm_page[i] = NULL;
		pinfo->vm_phys[i] = 0;
	}

	up_write(&mm->mmap_sem);
}

/*
 * Check if we need to lock the argument into scratch memory
 */
static int
need_to_lock(unsigned long addr, size_t len)
{
	struct aud_vm_info *vmi = ((struct aud_process *) current->audit)->vm_info;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct file	*f;
	unsigned long	end = addr + len;
	int 		res = 0;

	DPRINTF("pid %d, mm = %p, addr=%lx, len=%u\n", current->pid, mm, addr, len);
	for (vma = find_vma(mm, addr); addr < end; vma = vma->vm_next) {
		if (vma == NULL || addr < vma->vm_start) {
			res = -EFAULT;
			break;
		}

		addr = vma->vm_end;

		DPRINTF("vma=%p, start=%lx, end=%lx%s%s\n",
			 vma, vma->vm_start, vma->vm_end,
			 (vma->vm_flags & VM_SHARED)? ", shared" : "",
			 (vma->vm_flags & VM_WRITE)? ", write" : "");

		/* We always need to lock arguments residing in a
		 * shared VM area, because there may always be a way
		 * for an attacker to modify that shared area.
		 *
		 * The fact that _we_ mapped something read-only doesn't
		 * mean it's read-only for everyone else (think of mmapped
		 * files).
		 *
		 * We do want to optimize for the case where the
		 * underlying file is owned by root and not writable
		 * by anyone else (happens if the argument is a
		 * filename from a .rodata section of an ELF binary,
		 * for instance)
		 */
		if ((f = vma->vm_file) && f->f_dentry && f->f_dentry->d_inode) {
			struct inode	*inode = f->f_dentry->d_inode;

			if (S_ISREG(inode->i_mode)
			 && !inode->i_uid
			 && !(inode->i_mode & 022))
		       		continue;

			res |= AUD_ARG_NEED_LOCK | AUD_ARG_NEED_COPY;
		} else
		if (atomic_read(&vmi->refcnt) != 1) {
			/* entire VM shared with another process */
			res |= AUD_ARG_NEED_LOCK;
			if (vma->vm_flags & (VM_WRITE|VM_MAYWRITE))
				res |= AUD_ARG_NEED_COPY;
		}
	}

	return res;
}

/*
 * Copy argument from user space
 */
static int
do_copy_from_user(struct sysarg_data *target, void *arg, size_t len)
{
	int	res;

	res = need_to_lock((unsigned long) arg, len);
	DPRINTF("need_to_lock returns 0x%x\n", res);
	if (res < 0)
       		return res;
	target->at_flags |= res;
	if (target->at_flags & AUD_ARG_INOUT)
		target->at_flags &= ~AUD_ARG_NEED_COPY;

	if (copy_from_user(target->at_data.ptr, arg, len))
		return -EFAULT;

	return 0;
}

/*
 * Copy argument to scratch memory, and overwrite system call
 * argument. This will cause the kernel to look at the read-only
 * copy of the argument.
 */
static int
lock_argument(struct aud_syscall_data *sc, unsigned int n, struct sysarg_data *target)
{
	struct aud_process	*pinfo = (struct aud_process *) current->audit;
	size_t			copied, len, pad_len;
	int			err;

	len = target->at_data.len + 1;
	pad_len = (len + 7) & ~7;

	if (pinfo->vm_used + pad_len > pinfo->vm_size) {
		printk(KERN_NOTICE
			"audit: scratch memory too small to hold %lu bytes\n",
			(unsigned long) pad_len);
		return -EFAULT;
	}

	for (copied = 0; copied < len; ) {
		unsigned int	offset, bytes, nr;

		nr = (pinfo->vm_used + copied) >> PAGE_SHIFT;
		if ((err = audit_map_scratch_page(pinfo, nr)) < 0)
			return err;

		/* memcpy mustn't cross page boundary */
		offset = (pinfo->vm_used + copied) & ~PAGE_MASK;
		bytes = len - copied;
		if (bytes > PAGE_SIZE - offset)
			bytes = PAGE_SIZE - offset;

		memcpy((void *) (pinfo->vm_phys[nr] + offset),
				target->at_data.ptr + copied,
				bytes);

		flush_dcache_page(pinfo->vm_page[nr]);
		copied += bytes;
	}

	audit_update_arg(sc, n, pinfo->vm_addr + pinfo->vm_used);
	pinfo->vm_used += pad_len;

	return 0;
}

/*
 * Perform a realpath() sort of pathname resolution.
 * The buffer pointed to by target->path.name must be
 * allocated with __getname().
 *
 * XXX: need to lock path down to target
 */
static int
do_realpath(struct sysarg_data *target, void *arg, int arg_flags)
{
	struct nameidata nd;
	unsigned int	name_len, pathsize;
	int		error, flags, len, offset;
	char		*pathbuf, *slash, *str;

	/* strnlen_user includes the NUL charatcer */
	len = strnlen_user((char *) arg, PATH_MAX);
	if (len > PATH_MAX)
		len = PATH_MAX;
	else if (len <= 1)
		len = 1;

	if ((error = do_copy_from_user(target, arg, len)) < 0)
		return error;

	pathbuf  = target->at_path.name;
	pathsize = target->at_path.len;
	pathbuf[--len] = '\0';

	target->at_path.len = len;
	if (len == 0)
		return 0;

	DPRINTF("resolving [0x%p] \"%*.*s\"\n", arg, len, len, pathbuf);

	if ((target->at_flags & AUD_ARG_DIRNAME) || (arg_flags & O_NOFOLLOW))
		flags = LOOKUP_PARENT;
	else
		flags = LOOKUP_FOLLOW|LOOKUP_POSITIVE;

	slash = NULL;
	while (1) {
		memset(&nd, 0, sizeof(nd));
		error = -ENOBUFS;
		if (!path_init(pathbuf, flags, &nd))
			break;

		error = path_walk(pathbuf, &nd);
		if (error != -ENOENT)
			break;

		/* Shorten the path by one component */
		if (!(str = strrchr(pathbuf, '/')))
			break;
		while (str > pathbuf && str[-1] == '/')
			--str;
		if (str == pathbuf)
			break;

		if (slash)
			*slash = '/';
		slash = str;
		*slash = '\0';

		/* No need to do a path_release; path_walk does that
		 * for us in case of an error */
		flags = LOOKUP_FOLLOW|LOOKUP_POSITIVE;
	}

	if (error < 0)
		return error;

	/* Keep the dentry for matching purposes */
	target->at_path.dentry = dget(nd.dentry);
	target->at_path.exists = (slash == NULL);

	if (nd.last.len) {
		slash = (char *)nd.last.name;
		name_len = nd.last.len;
	} else if (slash) {
		slash++;
		name_len = strlen(slash);
	} else {
		/* slash is NULL */
		name_len = 0;
	}

	/* slash now points to the beginning of the last pathname component */

	/* If the file doesn't exist, we had to look up
	 * a parent directory instead. Move the trailing
	 * components out of the way so they don't get
	 * clobbered by the d_path call below. */
	if (slash) {
		pathsize -= name_len;
		memmove(pathbuf + pathsize, slash, name_len);
		slash = pathbuf + pathsize;
	}

	str = d_path(nd.dentry, nd.mnt, pathbuf, pathsize);
	if (IS_ERR(str)) {
		DPRINTF("d_path returns error %ld\n", PTR_ERR(str));
		return PTR_ERR(str);
	}

	len = strlen(str);
	if (str != pathbuf)
		memmove(pathbuf, str, len+1);
	DPRINTF("dir=%s len=%d\n", pathbuf, len);

	/* Attach the last path component (we've already made
	 * sure above that the buffer space is sufficient */
	if (name_len) {
		DPRINTF("last=%.*s\n", name_len, slash);
		if (pathbuf[0] == '/' && len == 1) {
			/* already at root level, don't add additional  '/' */
			offset = 0;
		} else {
			offset = 1;
			pathbuf[len] = '/';
		}
		memcpy(pathbuf + len + offset, slash, name_len);
		len += name_len + offset;
		pathbuf[len] = '\0';
	}
	target->at_path.len = len;
	DPRINTF("pathbuf=%s len=%d\n", pathbuf, len);

	path_release(&nd);
	return len;
}

/*
 * Copying this argument failed... try to deal with it.
 */
static int
__audit_fail_argument(struct sysarg_data *target, int error)
{
	/* Release any memory we may already have allocated to
	 * this argument. */
	__audit_release_arg(target);

	memset(target, 0, sizeof(*target));
	target->at_type = AUDIT_ARG_ERROR;
	target->at_intval = -error;
	return 0;
}

/*
 * Copy path name argument from user space and perform realpath()
 * on it
 */
static int
__audit_copy_pathname(struct sysarg_data *target, long value, int flags)
{
	char	*pathname;

	/* For pathnames, we want to perform a realpath()
	 * call here
	 */
	if (!(pathname = __getname()))
		return -1;
	target->at_path.name = pathname;
	target->at_type = AUDIT_ARG_PATH;
	target->at_path.len = PATH_MAX;
	target->at_path.dentry = NULL;

	if (do_realpath(target, (void *) value, flags) >= 0)
		return 0;

	memset(&target->at_path, 0, sizeof(target->at_path));
	putname(pathname);
	return -1;
}

/*
 * Copy file descriptor argument and try to get the path name
 * associated with it
 */
static int
__audit_copy_filedesc(struct sysarg_data *target, long value)
{
	char		*pathname = NULL, *str;
	struct file	*filp = NULL;
	struct inode	*inode;
	int		len, err = 0;

	filp = fget(value);
	if (!filp || !filp->f_dentry)
		goto bad_filedesc;

	if (!(pathname = __getname())) {
		err = -ENOBUFS;
		goto out;
	}

	target->at_path.name = pathname;
	target->at_type = AUDIT_ARG_PATH;
	target->at_path.len = PATH_MAX;
	target->at_path.dentry = NULL;

	inode = filp->f_dentry->d_inode;
	if (inode->i_sock) {
		struct socket   *sock = &inode->u.socket_i;

		snprintf(pathname, PATH_MAX, "[sock:af=%d,type=%d]",
				sock->ops->family, sock->type);
		len = strlen(pathname);
	} else {
		if (!filp->f_vfsmnt)
			goto bad_filedesc;
		str = d_path(filp->f_dentry, filp->f_vfsmnt, pathname, PATH_MAX);
		if (IS_ERR(str)) {
			err = PTR_ERR(str);
			goto out;
		}
		len = strlen(str);
		if (str != pathname)
			memmove(pathname, str, len+1);
	}

	DPRINTF("dir=%s\n", pathname);
	target->at_path.dentry = dget(filp->f_dentry);
	target->at_path.len = len;

out:	if (err < 0 && pathname)
{
		putname(pathname);
}
	if (filp)
		fput(filp);
	return err;

bad_filedesc:
	/* Bad filedesc - this is nothing to worry about,
	 * just flag it */
	target->at_type = AUDIT_ARG_ERROR;
	target->at_intval = EBADF;
	goto out;
}

/*
 * Copy arguments from user space
 */
static int
__audit_copy_from_user(struct aud_syscall_data *sc, struct sysarg *sysarg,
			struct sysarg_data *target, u_int64_t value)
{
	unsigned int	type, nitems;
	caddr_t		pvalue;
	size_t		len = 0;

	type = sysarg->sa_type;

	/* Interpret value as a pointer */
	pvalue = (caddr_t) (long) value;

	memset(target, 0, sizeof(*target));
	target->at_flags = sysarg->sa_flags;
	if (type == AUDIT_ARG_IMMEDIATE) {
		/* Sign extend argument to 64bit if necessary */
		if ((sysarg->sa_flags & AUD_ARG_SIGNED)
		 && (audit_syscall_word_size(sc) == 32
		  || (audit_syscall_word_size(sc) == 64 && sysarg->sa_size == 4)))
			value = (__s32)(value & 0xFFFFFFFFUL);
		target->at_type = AUDIT_ARG_IMMEDIATE;
		target->at_intval = value;
		return 0;
	}

	/* Pointer valued argument. First, check for NULL pointer */
	if (type != AUDIT_ARG_FILEDESC && value == 0) {
		target->at_type = AUDIT_ARG_NULL;
		target->at_data.ptr = NULL;
		target->at_data.len = 0;
		return 0;
	}

	/* Path names are special; we copy the string from user
	 * space _and_ perform a realpath() on it */
	if (type == AUDIT_ARG_PATH) {
		int	flags = 0;

		if (sc->major == __NR_open)
			flags = sc->raw_args[1];
		if (__audit_copy_pathname(target, value, flags) >= 0)
			return 0;

		/* Failed; treat it as string */
		memset(target, 0, sizeof(*target));
		type = AUDIT_ARG_STRING;
	} else if (type == AUDIT_ARG_FILEDESC) {
		return __audit_copy_filedesc(target, value);
	}

	switch (type) {
	case AUDIT_ARG_STRING:
		/* strnlen_user includes the NUL charatcer.
		 * We want to keep it, because we need to copy it
		 * to our scratch VM in case we decide the
		 * argument needs to be locked.
		 * We'll discard it later in encode_arguments
		 * when copying it to auditd. */
		len = strnlen_user(pvalue, PATH_MAX);
		if (len > PATH_MAX)
			len = PATH_MAX;
		break;

	case AUDIT_ARG_POINTER:
		len = sysarg->sa_size;
		break;

	case AUDIT_ARG_ARRAY:
		/* Arrays are pointers, with another
		 * syscall argument specifying the number
		 * of elements */
		nitems = sc->raw_args[sysarg->sa_ref];
		if (nitems > sysarg->sa_max)
			nitems = sysarg->sa_max;
		type = AUDIT_ARG_POINTER;
		len  = nitems * sysarg->sa_size;
		break;

	default:
		DPRINTF("unknown arg type %u\n", type);
		return -EINVAL;
	}

	target->at_type = type;
	if (len != 0) {
		int	err;

		target->at_data.ptr = mem_alloc(len, GFP_KERNEL);
		target->at_data.len = len;
		if (!target->at_data.ptr)
			return -ENOBUFS;

		if ((err = do_copy_from_user(target, pvalue, len)) < 0)
			return err;
	}

	return 0;
}

/*
 * Special case - copy argv[] type vector from user space
 */
static int
__audit_copy_vector(struct aud_syscall_data *sc, struct sysarg *sysarg,
			struct sysarg_data *target, u_int64_t value)
{
	struct sysarg_data *element;
	struct sysarg	elem_def;
	unsigned int	word_size, count;
	caddr_t		pvalue;
	size_t		total_size = 0;

	/* This must be set at run-time because the process
	 * could be either 32 or 64 bit */
	word_size = audit_syscall_word_size(sc) >> 3;

	/* Interpret value as a pointer */
	pvalue = (caddr_t) (long) value;

	/* Allocate memory for vector */
	count = AUDIT_MAX_ARGV * sizeof(element[0]);
	element = (struct sysarg_data *) kmalloc(count, GFP_KERNEL);
	memset(element, 0, count);

	/* Set up type info for the elements */
	memset(&elem_def, 0, sizeof(elem_def));
	elem_def.sa_type = sysarg->sa_ref;
	elem_def.sa_size = word_size;

	for (count = 0; count < AUDIT_MAX_ARGV; count++) {
		struct sysarg_data *elem_target = &element[count];
		u_int64_t	elem_value;
		int		r;

		/* For architectures that don't do 32/64 emulation,
		 * one of the branches should be optimized away */
		if (word_size == 4) {
			u_int32_t	raw32;

			r = copy_from_user(&raw32, pvalue + 4 * count, 4);
			elem_value = raw32;
		} else  {
			r = copy_from_user(&elem_value, pvalue + 8 * count, 8);
		}

		if (r != 0) {
			__audit_fail_argument(elem_target, -EFAULT);
			break;
		}
		if (elem_value == 0)
			break;

		__audit_copy_arg(sc, &elem_def, elem_target, elem_value);
		if (elem_target->at_type == AUDIT_ARG_STRING) {
			total_size += elem_target->at_data.len;
			if (total_size >= 2048)
				break;
		}
	}

	target->at_type = AUDIT_ARG_VECTOR;
	target->at_vector.elements = element;
	target->at_vector.count  = count;
	return 0;
}

static int
__audit_copy_arg(struct aud_syscall_data *sc, struct sysarg *sysarg,
			struct sysarg_data *target, u_int64_t value)
{
	int	r;

	/* See if we already have copied that argument */
	if (target->at_type != 0)
		return 0;

	if (sysarg->sa_type == AUDIT_ARG_VECTOR)
		r = __audit_copy_vector(sc, sysarg, target, value);
	else
		r = __audit_copy_from_user(sc, sysarg, target, value);
	if (r < 0)
		r = __audit_fail_argument(target, -r);
	return r;
}

static int
audit_copy_arg(struct aud_syscall_data *sc, unsigned int n)
{
	struct sysent	*entry;

	if (!(entry = sc->entry) || n >= entry->sy_narg)
		return -EINVAL;

	return __audit_copy_arg(sc, &entry->sy_args[n],
			&sc->args[n], sc->raw_args[n]);
}

struct sysarg_data *
audit_get_argument(struct aud_syscall_data *sc,
	       	   unsigned int n)
{
	int	err;

	err = audit_copy_arg(sc, n);
	if (err < 0)
		return ERR_PTR(err);
	return &sc->args[n];
}

int
audit_copy_arguments(struct aud_syscall_data *sc)
{
	unsigned int	n;
	int		err = 0;

	if (!sc || !sc->entry)
		return 0;

	for (n = 0; n < sc->entry->sy_narg && err >= 0; n++)
		err = audit_copy_arg(sc, n);
	return err;
}

int
audit_lock_arguments(struct aud_syscall_data *sc, int how)
{
	struct aud_process	*pinfo;
	struct sysarg_data	*target;
	unsigned int		n;
	int			err = 0, lock = 0;

	if (!sc || !sc->entry)
		return 0;

	/* If we run in relaxed paranoia mode, don't bother
	 * with locking the arguments in memory */
	if (audit_paranoia == 0 || current->mm == NULL)
		return 0;

	pinfo = (struct aud_process *) current->audit;

	audit_lock_vm(pinfo, how);
	if (!audit_validate_scratch_vm(pinfo)) {
		/* VM has changed. Re-map and re-lock it. */
		if ((err = audit_alloc_scratch_vm(pinfo)) < 0)
			return err;
		audit_lock_vm(pinfo, how);
	}

	/* We can use all the scratch pages we have */
	pinfo->vm_used = 0;

	for (n = 0; n < sc->entry->sy_narg; n++) {
		target = &sc->args[n];

		err = audit_copy_arg(sc, n);
		if (err < 0)
			break;

		/* Check if we need to copy this argument to
		 * non-shared memory */
		if (target->at_flags & AUD_ARG_NEED_COPY) {
			err = lock_argument(sc, n, target);
			if (err < 0)
				break;
		}
		lock |= target->at_flags;
	}

	if (!(lock & AUD_ARG_NEED_LOCK) || err < 0)
		audit_unlock_vm(pinfo);

	return err;
}

static void
__audit_release_arg(struct sysarg_data *target)
{
	switch (target->at_type) {
	case AUDIT_ARG_PATH:
		if (target->at_path.name)
			putname(target->at_path.name);
		if (target->at_path.dentry)
			dput(target->at_path.dentry);
		break;
	case AUDIT_ARG_STRING:
	case AUDIT_ARG_POINTER:
		if (target->at_data.ptr)
			mem_free(target->at_data.ptr);
		break;
	case AUDIT_ARG_VECTOR:
		if (target->at_vector.elements) {
			struct sysarg_data *element = target->at_vector.elements;
			unsigned int	count = target->at_vector.count;

			while (count--)
				__audit_release_arg(&element[count]);
			kfree(element);
		}
		break;
	}

	memset(target, 0, sizeof(*target));
}

void
audit_release_arguments(struct aud_process *pinfo)
{
	struct aud_syscall_data *sc = &pinfo->syscall;
	unsigned int	n;

	/* Unlock the VM, if we locked it */
	audit_unlock_vm(pinfo);

	/* Release memory allocated to hold arguments */
	if (sc && sc->entry) {
		for (n = sc->entry->sy_narg; n--; )
			__audit_release_arg(&sc->args[n]);;
	}
}

/* Forward decl */
static int __audit_encode_one(caddr_t, size_t,
	       		struct sysarg_data *, struct aud_syscall_data *);

/*
 * Encode elements of a vector
 */
static int
__audit_encode_vector(caddr_t dst, size_t dst_room,
			struct sysarg_data *target,
			struct aud_syscall_data *sc)
{
	unsigned int	len = 0, num;
	int		r;

	for (num = 0; num < target->at_vector.count; num++) {
		r = __audit_encode_one(dst,
				dst_room - 8 - len,
				&target->at_vector.elements[num], sc);
		if (r < 0)
			return r;
		if (dst)
			dst += r;
		len += r;
	}

	return len;
}

/*
 * Encode a single argument
 */
static int
__audit_encode_one(caddr_t dst, size_t dst_room, 
			struct sysarg_data *target,
			struct aud_syscall_data *sc)
{
	u_int32_t	type, len;
	void		*src;
	int		r;

	type = target->at_type;
	switch (type) {
	case AUDIT_ARG_IMMEDIATE:
	case AUDIT_ARG_ERROR:
		src = &target->at_intval;
		len = sizeof(target->at_intval);
		break;

	case AUDIT_ARG_PATH:
		src = target->at_path.name;
		len = target->at_path.len;
		break;

	case AUDIT_ARG_STRING:
		src = target->at_data.ptr;
		len = target->at_data.len;
		/* Do not copy the NUL byte to user space */
		if (len && ((char *) src)[len-1] == '\0')
			len--;
		break;

	case AUDIT_ARG_POINTER:
		src = target->at_data.ptr;
		len = target->at_data.len;
		break;

	case AUDIT_ARG_VECTOR:
		r = __audit_encode_vector(dst? dst + 8 : NULL, dst_room - 8, target, sc);
		if (r < 0)
			return r;
		src = NULL; /* elements already copied */
		len = r;
		break;

	default:
		src = NULL;
		len = 0;
	}

	if (dst != NULL) {
		if (len + 8 > dst_room)
			return -ENOBUFS;

		memcpy(dst, &type, 4); dst += 4;
		memcpy(dst, &len,  4); dst += 4;
DPRINTF("    copy %p len %u\n", src, len);
		if (src && len)
			memcpy(dst, src, len);
	}

	return len + 8;
}

/*
 * Encode all arguments
 */
int
audit_encode_args(void *data, size_t length,
			struct aud_syscall_data *sc)
{
	struct sysent	*entry = sc->entry;
	caddr_t	 	dst = (caddr_t) data;
	unsigned int	n, count = 0;
	int		len, error = 0;

	for (n = 0; n < entry->sy_narg; n++) {
		struct sysarg_data *target = &sc->args[n];

		if ((error = audit_copy_arg(sc, n)) < 0) {
			return error;
		}

		/* 8 is the room we need room for the end marker */
		len = __audit_encode_one(dst, length - 8 - count, target, sc);
		if (len < 0)
			return len;

		if (audit_debug > 1)
			DPRINTF("arg[%d]: type %d len %d\n",
				       	n, target->at_type, len);

		count += len;
		if (dst)
			dst += len;
	}

	/* Add the AUDIT_ARG_END marker */
	if (dst)
		memset(dst, 0, 8);
	count += 8;

	return count;
}

#ifdef DEBUG_MEMORY

#define MI_MAGIC	0xfeeb1e

struct mem_info {
	int		magic;
	list_t		entry;
	int		syscall;
	int		pid;
	unsigned long	when;
};

static spinlock_t mem_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(mem_list);
static unsigned long	mem_time;

static void *
mem_alloc(size_t len, int gfp)
{
	struct aud_process	*pinfo = (struct aud_process *) current->audit;
	struct mem_info		*mi;

	len += sizeof(*mi);
	if (!(mi = (struct mem_info *) kmalloc(len, gfp)))
		return NULL;

	mi->magic = MI_MAGIC;
	mi->syscall =  pinfo? pinfo->syscall.major : 0;
	mi->pid = current->pid;
	mi->when = jiffies + HZ / 10;

	spin_lock(&mem_lock);
	list_add(&mi->entry, &mem_list);
	spin_unlock(&mem_lock);

	return mi + 1;
}

void
mem_free(void *p)
{
	struct mem_info	*mi = ((struct mem_info *) p) - 1;

	BUG_ON(mi->magic != MI_MAGIC);
	spin_lock(&mem_lock);
	list_del_init(&mi->entry);
	p = mi;

	if (mem_time < jiffies) {
		list_t		*pos;
		unsigned long	cutoff = jiffies - HZ;
		int		count = 0;

		mem_time = jiffies + 30 * HZ;

		list_for_each(pos, &mem_list) {
			mi = list_entry(pos, struct mem_info, entry);

			if (mi->when > cutoff)
				continue;
			if (!count++)
				printk(KERN_NOTICE "--- Memory not freed ---\n");
			printk(KERN_NOTICE "  %p pid %5d, syscall %5d, age %ldsec\n",
					mi + 1, mi->pid, mi->syscall,
					(jiffies - mi->when) / HZ);
			if (count > 32)
				break;
		}
	}

	spin_unlock(&mem_lock);
	kfree(p);
}
#endif
