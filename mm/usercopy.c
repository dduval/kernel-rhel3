/*
 * linux/mm/usercopy.c
 *
 * (C) Copyright 2003 Ingo Molnar
 *
 * Generic implementation of all the user-VM access functions, without
 * relying on being able to access the VM directly.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/atomic_kmap.h>

/*
 * Get the kernel address of the user page and make it present.
 */
static struct page *fault_in_page(struct mm_struct *mm, unsigned long addr, int write)
{
	struct page *page = NULL;
	int ret;

	spin_unlock(&mm->page_table_lock);
	down_read(&mm->mmap_sem);
	ret = get_user_pages(current, mm, addr, 1, write, 0, &page, NULL);
	up_read(&mm->mmap_sem);
	spin_lock(&mm->page_table_lock);

	if (ret <= 0)
		return NULL;
	put_page(page);

	return page;
}

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space, 
 * Do not walk the page table directly, use get_user_pages
 */
static int rw_vm(unsigned long addr, void *buf, int len, int write)
{
	struct mm_struct *mm = current->mm ? : &init_mm;

	spin_lock(&mm->page_table_lock);
	/* ignore errors, just check how much was sucessfully transfered */
	while (len) {
		int bytes, offset;
		struct page *page;
		void *maddr;

		/*
		 * Do a quick atomic lookup first - this is the fastpath.
		 */
repeat:
		page = follow_page(mm, addr, write);
		/*
		 * Slowpath - bad address or needs to fault in the page:
		 */
		if (unlikely(!page)) {
			page = fault_in_page(mm, addr, write);
			if (unlikely(!page))
				break;
			/*
			 * Re-check the pagetable, with the spinlock
			 * held:
			 */
			goto repeat;
		}

		bytes = len;
		offset = addr & (PAGE_SIZE-1);
		if (unlikely(bytes > PAGE_SIZE-offset))
			bytes = PAGE_SIZE-offset;

		maddr = kmap_atomic(page, KM_USER_COPY);

#define HANDLE_TYPE(type) \
	case sizeof(type): *(type *)(maddr+offset) = *(type *)(buf); break;

		if (write) {
			switch (bytes) {
			HANDLE_TYPE(char);
			HANDLE_TYPE(int);
			HANDLE_TYPE(long long);
			default:
				memcpy(maddr + offset, buf, bytes);
			}
		} else {
#undef HANDLE_TYPE
#define HANDLE_TYPE(type) \
	case sizeof(type): *(type *)(buf) = *(type *)(maddr+offset); break;
			switch (bytes) {
			HANDLE_TYPE(char);
			HANDLE_TYPE(int);
			HANDLE_TYPE(long long);
			default:
				memcpy(buf, maddr + offset, bytes);
			}
#undef HANDLE_TYPE
		}
		kunmap_atomic(maddr, KM_USER_COPY);
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	spin_unlock(&mm->page_table_lock);
	return len;
}

static int str_vm(unsigned long addr, void *buf0, int len, int copy)
{
	struct mm_struct *mm = current->mm ? : &init_mm;
	struct page *page;
	void *buf = buf0;
	int write = (copy == 2);

	if (!len)
		return len;

	spin_lock(&mm->page_table_lock);
	/* ignore errors, just check how much was sucessfully transfered */
	while (len) {
		int bytes, ret, offset, left, copied;
		char *maddr;

		/*
		 * Do a quick atomic lookup first - this is the fastpath.
		 */
repeat:
		page = follow_page(mm, addr, write);
		/*
		 * Slowpath - bad address or needs to fault in the page:
		 */
		if (unlikely(!page)) {
			page = fault_in_page(mm, addr, write);
			if (unlikely(!page))
				goto bad_page;
			/*
			 * Re-check the pagetable, with the spinlock
			 * held:
			 */
			goto repeat;
		}

		if (unlikely(!page))
			goto bad_page;

		bytes = len;
		offset = addr & (PAGE_SIZE-1);
		if (bytes > PAGE_SIZE-offset)
			bytes = PAGE_SIZE-offset;

		maddr = kmap_atomic(page, KM_USER_COPY);
		if (copy == 2) {
			memset(maddr + offset, 0, bytes);
			copied = bytes;
			left = 0;
		} else if (copy == 1) {
			left = strncpy_count(buf, maddr + offset, bytes);
			copied = bytes - left;
		} else {
			copied = strnlen(maddr + offset, bytes);
			left = bytes - copied;
		}
		BUG_ON(bytes < 0 || copied < 0);
		kunmap_atomic(maddr, KM_USER_COPY);
		len -= copied;
		buf += copied;
		addr += copied;
		if (left)
			break;
	}
	spin_unlock(&mm->page_table_lock);
	return len;

bad_page:
	spin_unlock(&mm->page_table_lock);
	return -EFAULT;
}

/*
 * Copies memory from userspace (ptr) into kernelspace (val).
 *
 * returns # of bytes not copied.
 */
int get_user_size(unsigned int size, void *val, const void *ptr)
{
	int ret;

	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		memcpy(val, ptr, size);
		return 0;
	}
	ret = rw_vm((unsigned long)ptr, val, size, 0);
	if (ret)
		/*
		 * Zero the rest:
		 */
		memset(val + size - ret, 0, ret);
	return ret;
}

/*
 * Copies memory from kernelspace (val) into userspace (ptr).
 *
 * returns # of bytes not copied.
 */
int put_user_size(unsigned int size, const void *val, void *ptr)
{
	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		memcpy(ptr, val, size);
		return 0;
	}
	return rw_vm((unsigned long)ptr, (void *)val, size, 1);
}

int copy_str_fromuser_size(unsigned int size, void *val, const void *ptr)
{
	int copied, left;

	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		left = strncpy_count(val, ptr, size);
		copied = size - left;
		BUG_ON(copied < 0);

		return copied;
	}
	left = str_vm((unsigned long)ptr, val, size, 1);
	if (left < 0)
		return left;
	copied = size - left;
	BUG_ON(copied < 0);

	return copied;
}

int strlen_fromuser_size(unsigned int size, const void *ptr)
{
	int copied, left;

	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		copied = strnlen(ptr, size) + 1;
		BUG_ON(copied < 0);

		return copied;
	}
	left = str_vm((unsigned long)ptr, NULL, size, 0);
	if (left < 0)
		return 0;
	copied = size - left + 1;
	BUG_ON(copied < 0);

	return copied;
}

int zero_user_size(unsigned int size, void *ptr)
{
	int left;

	if (unlikely(segment_eq(get_fs(), KERNEL_DS))) {
		memset(ptr, 0, size);
		return 0;
	}
	left = str_vm((unsigned long)ptr, NULL, size, 2);
	if (left < 0)
		return size;
	return left;
}

EXPORT_SYMBOL(get_user_size);
EXPORT_SYMBOL(put_user_size);
EXPORT_SYMBOL(zero_user_size);
EXPORT_SYMBOL(copy_str_fromuser_size);
EXPORT_SYMBOL(strlen_fromuser_size);

