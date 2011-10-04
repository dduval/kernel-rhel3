/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also entry.S and others).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/memory.c': 'copy_page_range()'
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/namespace.h>
#include <linux/personality.h>
#include <linux/compiler.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/ptrace.h>
#include <linux/process_timing.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/processor.h>

extern int copy_semundo(unsigned long clone_flags, struct task_struct *tsk);
extern void exit_semundo(struct task_struct *tsk);

#include <linux/audit.h>

/* The idle threads do not count.. */
int nr_threads;

int max_threads;
unsigned long total_forks;	/* Handle normal Linux uptimes. */

rwlock_t tasklist_lock __cacheline_aligned = RW_LOCK_UNLOCKED;  /* outer */

/*
 * A per-CPU task cache - this relies on the fact that
 * the very last portion of sys_exit() is executed with
 * preemption turned off.
 */
static task_t *task_cache[NR_CPUS] __cacheline_aligned;

void __put_task_struct(struct task_struct *tsk)
{
	int cpu = smp_processor_id();

	free_uid(tsk->user);
	if (tsk != current) {
		__free_task_struct(tsk);
		return;
	}

	tsk = task_cache[cpu];
	if (tsk)
		__free_task_struct(tsk);

	task_cache[cpu] = current;
}

void add_wait_queue(wait_queue_head_t *q, wait_queue_t * wait)
{
	unsigned long flags;

	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

void add_wait_queue_exclusive_lifo(wait_queue_head_t *q, wait_queue_t * wait)
{
	unsigned long flags;

	wq_write_lock_irqsave(&q->lock, flags);
	wait->flags = WQ_FLAG_EXCLUSIVE;
	__add_wait_queue(q, wait);
	wq_write_unlock_irqrestore(&q->lock, flags);
}

void add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t * wait)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue_tail(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

void remove_wait_queue(wait_queue_head_t *q, wait_queue_t * wait)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__remove_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

void prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	__set_current_state(state);
	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

void
prepare_to_wait_exclusive(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	__set_current_state(state);
	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue_tail(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

void finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	if (!list_empty(&wait->task_list)) {
		spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		spin_unlock_irqrestore(&q->lock, flags);
	}
}

void __init fork_init(unsigned long mempages)
{
	/*
	 * The default maximum number of threads is set to a safe
	 * value: the thread structures can take up at most half
	 * of memory.
	 */
	max_threads = mempages / (THREAD_SIZE/PAGE_SIZE) / 8;
	/*
	 * we need to allow at least 20 threads to boot a system
	 */
	if(max_threads < 20)
		max_threads = 20;

	init_task.rlim[RLIMIT_NPROC].rlim_cur = max_threads/2;
	init_task.rlim[RLIMIT_NPROC].rlim_max = max_threads/2;
}

static struct task_struct *dup_task_struct(struct task_struct *orig)
{
	struct task_struct *tsk;
	int cpu = smp_processor_id();

	tsk = task_cache[cpu];
	task_cache[cpu] = NULL;

	if (!tsk) {
		tsk = __alloc_task_struct();
		if (!tsk)
			return NULL;
	}

	memcpy(tsk, orig, sizeof(*tsk));
	atomic_set(&tsk->usage,2);
	return tsk;
}

static inline int dup_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt, *tmp, **pprev;
	rb_node_t **rb_link, *rb_parent;
	int retval;
	unsigned long charge;

	flush_cache_mm(current->mm);
	mm->locked_vm = 0;
	mm->mmap = NULL;
	mm->mmap_cache = NULL;
	mm->map_count = 0;
	mm->free_area_cache = TASK_UNMAPPED_BASE;
	/* unlimited stack is larger than TASK_SIZE */
	mm->non_executable_cache = NON_EXECUTABLE_CACHE(current);
	mm->rss = 0;
	mm->cpu_vm_mask = 0;
	mm->mm_rb = RB_ROOT;
	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;

	/*
	 * Add it to the mmlist after the parent.
	 * Doing it this way means that we can order the list,
	 * and fork() won't mess up the ordering significantly.
	 * Add it first so that swapoff can see any swap entries.
	 */
	spin_lock(&mmlist_lock);
	list_add(&mm->mmlist, &current->mm->mmlist);
	mmlist_nr++;
	spin_unlock(&mmlist_lock);

	for (mpnt = current->mm->mmap ; mpnt ; mpnt = mpnt->vm_next) {
		struct file *file;

		retval = -ENOMEM;
		if(mpnt->vm_flags & VM_DONTCOPY)
			continue;
		charge = 0;
		if(mpnt->vm_flags & VM_ACCOUNT) {
			unsigned int len = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
			if(!vm_enough_memory(len))
				goto fail_nomem;
			charge = len;
		}
		tmp = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (!tmp)
			goto fail_nomem;
		*tmp = *mpnt;
		if (likely(!(tmp->vm_flags & VM_NO_UNLOCK)))
			tmp->vm_flags &= ~VM_LOCKED;
		tmp->vm_mm = mm;
		tmp->vm_next = NULL;
		file = tmp->vm_file;
		if (file) {
			struct inode *inode = file->f_dentry->d_inode;
			get_file(file);
			if (tmp->vm_flags & VM_DENYWRITE)
				atomic_dec(&inode->i_writecount);
      
			/* insert tmp into the share list, just after mpnt */
			spin_lock(&inode->i_mapping->i_shared_lock);
			if((tmp->vm_next_share = mpnt->vm_next_share) != NULL)
				mpnt->vm_next_share->vm_pprev_share =
					&tmp->vm_next_share;
			mpnt->vm_next_share = tmp;
			tmp->vm_pprev_share = &mpnt->vm_next_share;
			spin_unlock(&inode->i_mapping->i_shared_lock);
		}

		/*
		 * Link in the new vma and copy the page table entries:
		 * link in first so that swapoff can see swap entries,
		 * and try_to_unmap_one's find_vma find the new vma.
		 */
		spin_lock(&mm->page_table_lock);
		*pprev = tmp;
		pprev = &tmp->vm_next;

		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;

		mm->map_count++;
		retval = copy_page_range(mm, current->mm, tmp);
		spin_unlock(&mm->page_table_lock);

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		if (retval)
			goto fail_nomem;
	}
	retval = 0;
out:
	flush_tlb_mm(current->mm);
	return retval;
fail_nomem:
	vm_unacct_memory(charge);
	goto out;
}

spinlock_t mmlist_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
int mmlist_nr;

#define allocate_mm()	(kmem_cache_alloc(mm_cachep, SLAB_KERNEL))
#define free_mm(mm)	(kmem_cache_free(mm_cachep, (mm)))

static struct mm_struct * mm_init(struct mm_struct * mm)
{
	mm->ioctx_list = NULL;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	init_rwsem(&mm->mmap_sem);
	memset(&mm->mm_stat, 0, sizeof(mm->mm_stat));
	mm->core_waiters = 0;
	mm->page_table_lock = SPIN_LOCK_UNLOCKED;
	mm->free_area_cache = TASK_UNMAPPED_BASE;
	/* unlimited stack is larger than TASK_SIZE */
	mm->non_executable_cache = NON_EXECUTABLE_CACHE(current);
	mm->pgd = pgd_alloc(mm);
	mm->def_flags = 0;
	if (current->mm)
		mm->rlimit_rss = current->mm->rlimit_rss;
	else
		mm->rlimit_rss = RLIM_INFINITY;
	if (mm->pgd)
		return mm;
	free_mm(mm);
	return NULL;
}
	

/*
 * Allocate and initialize an mm_struct.
 */
struct mm_struct * mm_alloc(void)
{
	struct mm_struct * mm;

	mm = allocate_mm();
	if (mm) {
		memset(mm, 0, sizeof(*mm));
		return mm_init(mm);
	}
	return NULL;
}

/*
 * Called when the last reference to the mm
 * is dropped: either by a lazy thread or by
 * mmput. Free the page directory and the mm.
 */
inline void __mmdrop(struct mm_struct *mm)
{
	if (mm->ioctx_list)
		BUG();

	if (mm == &init_mm) BUG();
	pgd_free(mm->pgd);
	destroy_context(mm);
	free_mm(mm);
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	if (atomic_dec_and_lock(&mm->mm_users, &mmlist_lock)) {
		list_del(&mm->mmlist);
		mmlist_nr--;
		spin_unlock(&mmlist_lock);
		exit_aio(mm);
		exit_mmap(mm);
		mmdrop(mm);
	}
}

/* Please note the differences between mmput and mm_release.
 * mmput is called whenever we stop holding onto a mm_struct,
 * error success whatever.
 *
 * mm_release is called after a mm_struct has been removed
 * from the current process.
 *
 * This difference is important for error handling, when we
 * only half set up a mm_struct for a new process and need to restore
 * the old one.  Because we mmput the new mm_struct before
 * restoring the old one. . .
 * Eric Biederman 10 January 1998
 */
void mm_release(void)
{
	struct task_struct *tsk = current;
	struct completion *vfork_done = tsk->vfork_done;

	/* notify parent sleeping on vfork() */
	if (vfork_done) {
		tsk->vfork_done = NULL;
		complete(vfork_done);
	}
	if (tsk->clear_child_tid) {
		u32 * tidptr = tsk->clear_child_tid;
		tsk->clear_child_tid = NULL;
		/*
		 * We dont check the error code - if userspace has
		 * not set up a proper pointer then tough luck.
		 */
		put_user(0, tidptr);
		sys_futex(tidptr, FUTEX_WAKE, 1, NULL, NULL, 0);
	}
}

static int copy_mm(unsigned long clone_flags, struct task_struct * tsk)
{
	struct mm_struct * mm, *oldmm;
	int retval;

	tsk->min_flt = tsk->maj_flt = 0;
	tsk->cmin_flt = tsk->cmaj_flt = 0;
	tsk->nswap = tsk->cnswap = 0;

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	/*
	 * Are we cloning a kernel thread?
	 *
	 * We need to steal a active VM for that..
	 */
	oldmm = current->mm;
	if (!oldmm)
		return 0;

	if (clone_flags & CLONE_VM) {
		atomic_inc(&oldmm->mm_users);
		mm = oldmm;

		/*
		 * There are cases where the PTL is held to ensure no
		 * new threads start up in user mode using an mm, which
		 * allows optimizing out ipis; the tlb_gather_mmu code
		 * is an example.
		 */
		spin_unlock_wait(&oldmm->page_table_lock);
		goto good_mm;
	}

	retval = -ENOMEM;
	mm = allocate_mm();
	if (!mm)
		goto fail_nomem;

	/* Copy the current MM stuff.. */
	memcpy(mm, oldmm, sizeof(*mm));
	if (!mm_init(mm))
		goto fail_nomem;

	if (init_new_context(tsk,mm))
		goto free_pt;

	down_write(&oldmm->mmap_sem);
	retval = dup_mmap(mm);
	up_write(&oldmm->mmap_sem);

	if (retval)
		goto free_pt;

good_mm:
	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;

free_pt:
	mmput(mm);
fail_nomem:
	return retval;
}

static inline struct fs_struct *__copy_fs_struct(struct fs_struct *old)
{
	struct fs_struct *fs = kmem_cache_alloc(fs_cachep, GFP_KERNEL);
	/* We don't need to lock fs - think why ;-) */
	if (fs) {
		atomic_set(&fs->count, 1);
		fs->lock = RW_LOCK_UNLOCKED;
		fs->umask = old->umask;
		read_lock(&old->lock);
		fs->rootmnt = mntget(old->rootmnt);
		fs->root = dget(old->root);
		fs->pwdmnt = mntget(old->pwdmnt);
		fs->pwd = dget(old->pwd);
		if (old->altroot) {
			fs->altrootmnt = mntget(old->altrootmnt);
			fs->altroot = dget(old->altroot);
		} else {
			fs->altrootmnt = NULL;
			fs->altroot = NULL;
		}	
		read_unlock(&old->lock);
	}
	return fs;
}

struct fs_struct *copy_fs_struct(struct fs_struct *old)
{
	return __copy_fs_struct(old);
}

static inline int copy_fs(unsigned long clone_flags, struct task_struct * tsk)
{
	if (clone_flags & CLONE_FS) {
		atomic_inc(&current->fs->count);
		return 0;
	}
	tsk->fs = __copy_fs_struct(current->fs);
	if (!tsk->fs)
		return -1;
	return 0;
}

static int count_open_files(struct files_struct *files, int size)
{
	int i;
	
	/* Find the last open fd */
	for (i = size/(8*sizeof(long)); i > 0; ) {
		if (files->open_fds->fds_bits[--i])
			break;
	}
	i = (i+1) * 8 * sizeof(long);
	return i;
}

static int copy_files(unsigned long clone_flags, struct task_struct * tsk)
{
	struct files_struct *oldf, *newf;
	struct file **old_fds, **new_fds;
	int open_files, nfds, size, i, error = 0;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {
		atomic_inc(&oldf->count);
		goto out;
	}

	/*
	 * Note: we may be using current for both targets (See exec.c)
	 * This works because we cache current->files (old) as oldf. Don't
	 * break this.
	 */
	tsk->files = NULL;
	error = -ENOMEM;
	newf = kmem_cache_alloc(files_cachep, SLAB_KERNEL);
	if (!newf) 
		goto out;

	atomic_set(&newf->count, 1);

	newf->file_lock	    = RW_LOCK_UNLOCKED;
	newf->next_fd	    = 0;
	newf->max_fds	    = NR_OPEN_DEFAULT;
	newf->max_fdset	    = __FD_SETSIZE;
	newf->close_on_exec = &newf->close_on_exec_init;
	newf->open_fds	    = &newf->open_fds_init;
	newf->fd	    = &newf->fd_array[0];

	/* We don't yet have the oldf readlock, but even if the old
           fdset gets grown now, we'll only copy up to "size" fds */
	size = oldf->max_fdset;
	if (size > __FD_SETSIZE) {
		newf->max_fdset = 0;
		write_lock(&newf->file_lock);
		error = expand_fdset(newf, size-1);
		write_unlock(&newf->file_lock);
		if (error)
			goto out_release;
	}
	read_lock(&oldf->file_lock);

	open_files = count_open_files(oldf, size);

	/*
	 * Check whether we need to allocate a larger fd array.
	 * Note: we're not a clone task, so the open count won't
	 * change.
	 */
	nfds = NR_OPEN_DEFAULT;
	if (open_files > nfds) {
		read_unlock(&oldf->file_lock);
		newf->max_fds = 0;
		write_lock(&newf->file_lock);
		error = expand_fd_array(newf, open_files-1);
		write_unlock(&newf->file_lock);
		if (error) 
			goto out_release;
		nfds = newf->max_fds;
		read_lock(&oldf->file_lock);
	}

	old_fds = oldf->fd;
	new_fds = newf->fd;

	memcpy(newf->open_fds->fds_bits, oldf->open_fds->fds_bits, open_files/8);
	memcpy(newf->close_on_exec->fds_bits, oldf->close_on_exec->fds_bits, open_files/8);

	for (i = open_files; i != 0; i--) {
		struct file *f = *old_fds++;
		if (f) {
			get_file(f);
		} else {
			/*
			 * The fd may be claimed in the fd bitmap but not yet
			 * instantiated in the files array if a sibling thread
			 * is partway through open().  So make sure that this
			 * fd is available to the new process.
			 */
			FD_CLR(open_files - i, newf->open_fds);
		}
		*new_fds++ = f;
	}
	read_unlock(&oldf->file_lock);

	/* compute the remainder to be cleared */
	size = (newf->max_fds - open_files) * sizeof(struct file *);

	/* This is long word aligned thus could use a optimized version */ 
	memset(new_fds, 0, size); 

	if (newf->max_fdset > open_files) {
		int left = (newf->max_fdset-open_files)/8;
		int start = open_files / (8 * sizeof(unsigned long));
		
		memset(&newf->open_fds->fds_bits[start], 0, left);
		memset(&newf->close_on_exec->fds_bits[start], 0, left);
	}

	tsk->files = newf;
	error = 0;
out:
	return error;

out_release:
	free_fdset (newf->close_on_exec, newf->max_fdset);
	free_fdset (newf->open_fds, newf->max_fdset);
	kmem_cache_free(files_cachep, newf);
	goto out;
}

/*
 *	Helper to unshare the files of the current task. 
 *	We don't want to expose copy_files internals to 
 *	the exec layer of the kernel.
 */

int unshare_files(void)
{
	struct files_struct *files  = current->files;
	int rc;
	
	if(!files)
		BUG();
		
	/* This can race but the race causes us to copy when we don't
	   need to and drop the copy */
	if(atomic_read(&files->count) == 1)
	{
		atomic_inc(&files->count);
		return 0;
	}
	rc = copy_files(0, current);
	if(rc)
		current->files = files;
	return rc;
}		

static inline int copy_sighand(unsigned long clone_flags, struct task_struct * tsk)
{
	struct sighand_struct *sig;

	if (clone_flags & (CLONE_SIGHAND | CLONE_THREAD)) {
		atomic_inc(&current->sighand->count);
		return 0;
	}
	sig = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	tsk->sighand = sig;
	if (!sig)
		return -1;
	spin_lock_init(&sig->siglock);
	atomic_set(&sig->count, 1);
	memcpy(sig->action, current->sighand->action, sizeof(sig->action));
	return 0;
}

static inline int copy_signal(unsigned long clone_flags, struct task_struct * tsk)
{
	struct signal_struct *sig;

	if (clone_flags & CLONE_THREAD) {
		atomic_inc(&current->signal->count);
		return 0;
	}
	sig = kmem_cache_alloc(signal_cachep, GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -1;
	atomic_set(&sig->count, 1);
	sig->group_exit = 0;
	sig->group_exit_code = 0;
	sig->group_exit_task = NULL;
	sig->group_stop_count = 0;
	sig->curr_target = NULL;
	init_sigpending(&sig->shared_pending);

	return 0;
}

static inline void copy_flags(unsigned long clone_flags, struct task_struct *p)
{
	unsigned long new_flags = p->flags;

	new_flags &= ~(PF_SUPERPRIV | PF_USEDFPU);
	new_flags |= PF_FORKNOEXEC;
	if (!(clone_flags & CLONE_PTRACE))
		p->ptrace &= PT_AUDITED;
	p->flags = new_flags;
}

long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct task_struct *task = current;
	unsigned old_task_dumpable;
	long ret;

	/* lock out any potential ptracer */
	task_lock(task);
	if (task->ptrace & ~PT_AUDITED) {
		task_unlock(task);
		return -EPERM;
	}

	old_task_dumpable = task->task_dumpable;
	task->task_dumpable = 0;
	task_unlock(task);

	ret = arch_kernel_thread(fn, arg, flags);

	/* never reached in child process, only in parent */
	current->task_dumpable = old_task_dumpable;

	return ret;
}

asmlinkage long sys_set_tid_address(int *tidptr)
{
	current->clear_child_tid = tidptr;

	return current->pid;
}

/*
 * This creates a new process as a copy of the old one,
 * but does not actually start it yet.
 *
 * It copies the registers, and all the appropriate
 * parts of the process environment (as per the clone
 * flags).  The actual kick-off is left to the caller.
 */
struct task_struct *copy_process(unsigned long clone_flags,
			         unsigned long stack_start,
			         struct pt_regs *regs,
			         unsigned long stack_size,
			         int *parent_tidptr,
			         int *child_tidptr)
{
	int retval;
	struct task_struct *p = NULL;

	if ((clone_flags & (CLONE_NEWNS|CLONE_FS)) == (CLONE_NEWNS|CLONE_FS))
		return ERR_PTR(-EINVAL);

	/*
	 * Thread groups must share signals as well, and detached threads
	 * can only be started up within the thread group.
	 */
	if ((clone_flags & CLONE_THREAD) && !(clone_flags & CLONE_SIGHAND))
		return ERR_PTR(-EINVAL);
	if ((clone_flags & CLONE_DETACHED) && !(clone_flags & CLONE_THREAD))
		return ERR_PTR(-EINVAL);
	if (!(clone_flags & CLONE_DETACHED) && (clone_flags & CLONE_THREAD))
		return ERR_PTR(-EINVAL);

	retval = -ENOMEM;
	p = dup_task_struct(current);
	if (!p)
		goto fork_out;

	p->tux_info = NULL;

	retval = -EAGAIN;

	/*
	 * Increment user->__count before the rlimit test so that it would
	 * be correct if we take the bad_fork_free failure path.
	 */
	atomic_inc(&p->user->__count);
	if (atomic_read(&p->user->processes) >= p->rlim[RLIMIT_NPROC].rlim_cur) {
		if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RESOURCE))
			goto bad_fork_free;
	}

	atomic_inc(&p->user->processes);

	/*
	 * Counter increases are protected by
	 * the kernel lock so nr_threads can't
	 * increase under us (but it may decrease).
	 */
	if (nr_threads >= max_threads)
		goto bad_fork_cleanup_count;
	
	get_exec_domain(p->exec_domain);

	if (p->binfmt && p->binfmt->module)
		__MOD_INC_USE_COUNT(p->binfmt->module);

	p->did_exec = 0;
	p->swappable = 0;
	p->state = TASK_UNINTERRUPTIBLE;

	copy_flags(clone_flags, p);
	if (clone_flags & CLONE_IDLETASK)
		p->pid = 0;
	else {
		p->pid = alloc_pidmap();
		if (p->pid == -1)
			goto bad_fork_cleanup;
	}
	
	retval = -EFAULT;
	if (clone_flags & CLONE_PARENT_SETTID)
		if (put_user(p->pid, parent_tidptr))
			goto bad_fork_cleanup;

	INIT_LIST_HEAD(&p->run_list);

	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	init_waitqueue_head(&p->wait_chldexit);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);
	spin_lock_init(&p->switch_lock);

	p->sigpending = 0;
	init_sigpending(&p->pending);

	p->it_real_value = p->it_virt_value = p->it_prof_value = 0;
	p->it_real_incr = p->it_virt_incr = p->it_prof_incr = 0;
	init_timer(&p->real_timer);
	p->real_timer.data = (unsigned long) p;

	p->leader = 0;		/* session leadership doesn't inherit */
	p->tty_old_pgrp = 0;
	memset(&p->utime, 0, sizeof(p->utime));
	memset(&p->stime, 0, sizeof(p->stime));
	memset(&p->cutime, 0, sizeof(p->cutime));
	memset(&p->cstime, 0, sizeof(p->cstime));
	memset(&p->group_utime, 0, sizeof(p->group_utime));
	memset(&p->group_stime, 0, sizeof(p->group_stime));
	memset(&p->group_cutime, 0, sizeof(p->group_cutime));
	memset(&p->group_cstime, 0, sizeof(p->group_cstime));

#ifdef CONFIG_SMP
	memset(&p->per_cpu_utime, 0, sizeof(p->per_cpu_utime));
	memset(&p->per_cpu_stime, 0, sizeof(p->per_cpu_stime));
#endif

	memset(&p->timing_state, 0, sizeof(p->timing_state));
	p->timing_state.type = PROCESS_TIMING_USER;
	p->last_sigxcpu = 0;
	p->array = NULL;
	p->lock_depth = -1;		/* -1 = no lock */
	p->start_time = jiffies;

	retval = -ENOMEM;
	/* copy all the process information */
	if (copy_files(clone_flags, p))
		goto bad_fork_cleanup;
	if (copy_fs(clone_flags, p))
		goto bad_fork_cleanup_files;
	if (copy_sighand(clone_flags, p))
		goto bad_fork_cleanup_fs;
	if (copy_signal(clone_flags, p))
		goto bad_fork_cleanup_sighand;
	if (copy_mm(clone_flags, p))
		goto bad_fork_cleanup_signal;
	if (copy_namespace(clone_flags, p))
		goto bad_fork_cleanup_mm;
	retval = copy_thread(0, clone_flags, stack_start, stack_size, p, regs);
	if (retval)
		goto bad_fork_cleanup_namespace;
	p->semundo = NULL;

	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID)
		? child_tidptr : NULL;
	/*
	 * Clear TID on mm_release()?
	 */
	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID)
		? child_tidptr : NULL;

	/* Our parent execution domain becomes current domain
	   These must match for thread signalling to apply */
	   
	p->parent_exec_id = p->self_exec_id;

	/* ok, now we should be set up.. */
	p->swappable = 1;
	if (clone_flags & CLONE_DETACHED)
		p->exit_signal = -1;
	else
		p->exit_signal = clone_flags & CSIGNAL;
	p->pdeath_signal = 0;

	/*
	 * Share the timeslice between parent and child, thus the
	 * total amount of pending timeslices in the system doesnt change,
	 * resulting in more scheduling fairness.
	 */
	local_irq_disable();
	p->time_slice = (current->time_slice + 1) >> 1;
	p->first_time_slice = 1;
	/*
	 * The remainder of the first timeslice might be recovered by
	 * the parent if the child exits early enough.
	 */
	current->time_slice >>= 1;
	p->last_run = jiffies;
	if (!current->time_slice) {
		/*
		 * This case is rare, it happens when the parent has only
		 * a single jiffy left from its timeslice. Taking the
		 * runqueue lock is not a problem.
		 */
		current->time_slice = 1;
		scheduler_tick(0 /* don't update the time stats */);
	}
	local_irq_enable();

	if ((int)current->time_slice <= 0)
		BUG();
	if ((int)p->time_slice <= 0)
		BUG();

	/*
	 * Ok, add it to the run-queues and make it
	 * visible to the rest of the system.
	 *
	 * Let it rip!
	 */
	p->tgid = p->pid;
	p->group_leader = p;
	INIT_LIST_HEAD(&p->ptrace_children);
	INIT_LIST_HEAD(&p->ptrace_list);

	/* Need tasklist lock for parent etc handling! */
	write_lock_irq(&tasklist_lock);
	/*
	 * Check for pending SIGKILL! The new thread should not be allowed
	 * to slip out of an OOM kill. (or normal SIGKILL.)
	 */
	if (sigismember(&current->pending.signal, SIGKILL)) {
		write_unlock_irq(&tasklist_lock);
		retval = -EINTR;
		goto bad_fork_cleanup_namespace;
	}

	/* CLONE_PARENT re-uses the old parent */
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD))
		p->real_parent = current->real_parent;
	else
		p->real_parent = current;
	p->parent = p->real_parent;

	if (clone_flags & CLONE_THREAD) {
		spin_lock(&current->sighand->siglock);
		/*
		 * Important: if an exit-all has been started then
		 * do not create this new thread - the whole thread
		 * group is supposed to exit anyway.
		 */
		if (current->signal->group_exit) {
			spin_unlock(&current->sighand->siglock);
			write_unlock_irq(&tasklist_lock);
			retval = -EINTR;
			goto bad_fork_cleanup_namespace;
		}
		p->tgid = current->tgid;
		p->group_leader = current->group_leader;

		if (current->signal->group_stop_count > 0) {
			/*
			 * There is an all-stop in progress for the group.
			 * We ourselves will stop as soon as we check signals.
			 * Make the new thread part of that group stop too.
			 */
			current->signal->group_stop_count++;
			p->sigpending = 1;
		}

		spin_unlock(&current->sighand->siglock);
	}

	SET_LINKS(p);
	if (p->ptrace & PT_PTRACED)
		__ptrace_link(p, current->parent);

	attach_pid(p, PIDTYPE_PID, p->pid);
	if (thread_group_leader(p)) {
		attach_pid(p, PIDTYPE_TGID, p->tgid);
		attach_pid(p, PIDTYPE_PGID, p->pgrp);
		attach_pid(p, PIDTYPE_SID, p->session);
	} else {
		link_pid(p, p->pids + PIDTYPE_TGID,
			&p->group_leader->pids[PIDTYPE_TGID].pid);
	}

	/* clear controlling tty of new task if parent's was just cleared */
	if (!current->tty && p->tty)
		p->tty = NULL;

	nr_threads++;
	write_unlock_irq(&tasklist_lock);
	retval = 0;

fork_out:
	if (retval)
		return ERR_PTR(retval);
	return p;

bad_fork_cleanup_namespace:
	exit_namespace(p);
bad_fork_cleanup_mm:
	exit_mm(p);
	if (p->active_mm)
		mmdrop(p->active_mm);
bad_fork_cleanup_signal:
	exit_signal(p);
bad_fork_cleanup_sighand:
	exit_sighand(p);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup:
	if (p->pid > 0)
		free_pidmap(p->pid);
	put_exec_domain(p->exec_domain);
	if (p->binfmt && p->binfmt->module)
		__MOD_DEC_USE_COUNT(p->binfmt->module);
bad_fork_cleanup_count:
	atomic_dec(&p->user->processes);
bad_fork_free:
	p->state = TASK_ZOMBIE; /* debug */
	atomic_dec(&p->usage);
	put_task_struct(p);
	goto fork_out;
}

static inline int fork_traceflag (unsigned clone_flags)
{
	if (clone_flags & (CLONE_UNTRACED | CLONE_IDLETASK))
		return 0;
	else if (clone_flags & CLONE_VFORK) {
		if (current->ptrace & PT_TRACE_VFORK)
			return PTRACE_EVENT_VFORK;
	} else if ((clone_flags & CSIGNAL) != SIGCHLD) {
		if (current->ptrace & PT_TRACE_CLONE)
			return PTRACE_EVENT_CLONE;
	} else if (current->ptrace & PT_TRACE_FORK)
		return PTRACE_EVENT_FORK;

	return 0;
}

/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 */
int do_fork(unsigned long clone_flags,
	    unsigned long stack_start,
	    struct pt_regs *regs,
	    unsigned long stack_size,
	    int *parent_tidptr,
	    int *child_tidptr)
{
	struct task_struct *p;
	int trace = 0;
	pid_t pid;

	if (unlikely(current->ptrace)) {
		trace = fork_traceflag (clone_flags);
		if (trace)
			clone_flags |= CLONE_PTRACE;
	}

	p = copy_process(clone_flags, stack_start, regs, stack_size, 
			 parent_tidptr, child_tidptr);

	if (unlikely(IS_ERR(p))) 
		return (int) PTR_ERR(p);
	else {
		struct completion vfork;

         	/*
	         * Do this prior waking up the new thread - the thread pointer
       	  	 * might get invalid after that point, if the thread exits 
		 * quickly.
       		 */
		pid = p->pid;
		if (clone_flags & CLONE_VFORK) {
			p->vfork_done = &vfork;
			init_completion(&vfork);
		}

		if ((p->ptrace & PT_PTRACED) || (clone_flags & CLONE_STOPPED)) {
			/*
			 * We'll start up with an immediate SIGSTOP.
			 */
			sigaddset(&p->pending.signal, SIGSTOP);
			p->sigpending = 1;
		}

		if (isaudit(current))
			audit_fork(current, p);

		/*
		 * The task is in TASK_UNINTERRUPTIBLE right now, no-one
		 * can wake it up. Either wake it up as a child, which
		 * makes it TASK_RUNNING - or make it TASK_STOPPED, after
		 * which signals can wake the child up.
		 */
		if (!(clone_flags & CLONE_STOPPED))
			wake_up_forked_process(p);	/* do this last */
		else
			p->state = TASK_STOPPED;
		++total_forks;

		if (unlikely (trace)) {
			current->ptrace_message = (unsigned long) pid;
			ptrace_notify((trace << 8) | SIGTRAP);
		}

		if (clone_flags & CLONE_VFORK) {
			wait_for_completion(&vfork);
			if (unlikely(current->ptrace & PT_TRACE_VFORK_DONE))
				ptrace_notify((PTRACE_EVENT_VFORK_DONE << 8) | SIGTRAP);
		} else
			/*
			 * Let the child process run first, to avoid most of the
			 * COW overhead when the child exec()s afterwards.
			 */
			set_need_resched();
	}
	return pid;
}

/* SLAB cache for signal_struct structures (tsk->signal) */
kmem_cache_t *signal_cachep;

/* SLAB cache for sighand_struct structures (tsk->sighand) */
kmem_cache_t *sighand_cachep;

/* SLAB cache for files_struct structures (tsk->files) */
kmem_cache_t *files_cachep;

/* SLAB cache for fs_struct structures (tsk->fs) */
kmem_cache_t *fs_cachep;

/* SLAB cache for vm_area_struct structures */
kmem_cache_t *vm_area_cachep;

/* SLAB cache for mm_struct structures (tsk->mm) */
kmem_cache_t *mm_cachep;

void __init proc_caches_init(void)
{
	sighand_cachep = kmem_cache_create("sighand_cache",
			sizeof(struct sighand_struct), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!sighand_cachep)
		panic("Cannot create sighand SLAB cache");

	signal_cachep = kmem_cache_create("signal_cache",
			sizeof(struct signal_struct), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!signal_cachep)
		panic("Cannot create signal SLAB cache");

	files_cachep = kmem_cache_create("files_cache", 
			 sizeof(struct files_struct), 0, 
			 SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!files_cachep) 
		panic("Cannot create files SLAB cache");

	fs_cachep = kmem_cache_create("fs_cache", 
			 sizeof(struct fs_struct), 0, 
			 SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!fs_cachep) 
		panic("Cannot create fs_struct SLAB cache");
 
	vm_area_cachep = kmem_cache_create("vm_area_struct",
			sizeof(struct vm_area_struct), 0,
			0, NULL, NULL);
	if(!vm_area_cachep)
		panic("vma_init: Cannot alloc vm_area_struct SLAB cache");

	mm_cachep = kmem_cache_create("mm_struct",
			sizeof(struct mm_struct), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!mm_cachep)
		panic("vma_init: Cannot alloc mm_struct SLAB cache");
}

EXPORT_SYMBOL(copy_fs_struct);
