#include <linux/worktodo.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

static void __wtd_lock_page_waiter(wait_queue_t *wait)
{
	struct worktodo *wtd = (struct worktodo *)wait;
	struct page *page = (struct page *)wtd->data;

	if (!TryLockPage(page)) {
		__remove_wait_queue(page_waitqueue(page), &wtd->wait);
		wtd_queue(wtd);
	} else
		schedule_task(&run_disk_tq);
}

int wtd_lock_page(struct worktodo *wtd, struct page *page)
{
	if (TryLockPage(page)) {
		wtd->data = page;
		init_waitqueue_func_entry(&wtd->wait, __wtd_lock_page_waiter);

		/* Wakeups may race with TryLockPage, so try again within the wait 
		 * queue spinlock.
		 */
		if (!add_wait_queue_cond(page_waitqueue(page), &wtd->wait,
					TryLockPage(page))) {
			/* Page is still locked.  Kick the disk queue... */
			run_task_queue(&tq_disk);
			return 0;
		}
	}

	return 1;
}

static void __wtd_bh_waiter(wait_queue_t *wait)
{
	struct worktodo *wtd = (struct worktodo *)wait;
	struct buffer_head *bh = (struct buffer_head *)wtd->data;

	if (!buffer_locked(bh)) {
		__remove_wait_queue(&bh->b_wait, &wtd->wait);
		wtd_queue(wtd);
	} else {
		schedule_task(&run_disk_tq);
	}
}

int wtd_wait_on_buffer(struct worktodo *wtd, struct buffer_head *bh)
{
	if (!buffer_locked(bh)) {
		return 1;
	}
	wtd->data = bh;
	init_waitqueue_func_entry(&wtd->wait, __wtd_bh_waiter);
	if (add_wait_queue_cond(&bh->b_wait, &wtd->wait, buffer_locked(bh)))
		return 1;
	run_task_queue(&tq_disk);
	return 0;
}

void do_run_tq_disk(void *data)
{
	run_task_queue(&tq_disk);
}

struct tq_struct run_disk_tq = {
	routine: do_run_tq_disk,
	data: NULL
};

