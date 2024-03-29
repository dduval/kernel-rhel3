/*
 * linux/arch/i386/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

/*
 * The exception table needs to be sorted because we use the macros
 * which put things into the exception table in a variety of sections
 * as well as the init section and the main kernel text section.
 */
static inline void
sort_ex_table(struct exception_table_entry *start,
	      struct exception_table_entry *finish)
{
	struct exception_table_entry el, *p, *q;

	/* insertion sort */
	for (p = start + 1; p < finish; ++p) {
		/* start .. p-1 is sorted */
		if (p[0].insn < p[-1].insn) {
			/* move element p down to its right place */
			el = *p;
			q = p;
			do {
				/* el comes before q[-1], move q[-1] up one */
				q[0] = q[-1];
				--q;
			} while (q > start && el.insn < q[-1].insn);
			*q = el;
		}
	}
}

void fixup_sort_exception_table(void)
{
	struct exception_table_entry *p;

	/*
	 * Fix up the trampoline exception addresses:
	 */
	for (p = __start___ex_table; p < __stop___ex_table; p++) {
		p->insn = (unsigned long)ENTRY_TRAMP_ADDR((void *)p->insn);
		p->fixup = (unsigned long)ENTRY_TRAMP_ADDR((void *)p->fixup);
	}
	sort_ex_table(__start___ex_table, __stop___ex_table);
}

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;

		mid = (last - first) / 2 + first;
		/*
		 * careful, the distance between entries can be
		 * larger than 2GB:
		 */
                if (mid->insn == value)
                        return mid->fixup;
                else if (mid->insn < value)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

extern spinlock_t modlist_lock;

unsigned long
search_exception_table(unsigned long addr)
{
	unsigned long ret = 0;
	
#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table, __stop___ex_table-1, addr);
	return ret;
#else
	unsigned long flags;
	/* The kernel is the last "module" -- no need to treat it special.  */
	struct module *mp;

	spin_lock_irqsave(&modlist_lock, flags);
	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (mp->ex_table_start == NULL || !(mp->flags&(MOD_RUNNING|MOD_INITIALIZING)))
			continue;
		ret = search_one_table(mp->ex_table_start,
				       mp->ex_table_end - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}
