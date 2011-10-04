#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/config.h>
#ifdef CONFIG_KALLSYMS
#include <linux/kallsyms.h>
#endif



int lookup_symbol(unsigned long address, char *buffer, int buflen)
{
	struct module *this_mod;
	unsigned long bestsofar;

	const char *mod_name = NULL, *sec_name = NULL, *sym_name = NULL;
	unsigned long mod_start,mod_end,sec_start,sec_end,sym_start,sym_end;
	
	if (!buffer)
		return -EFAULT;
	
	if (buflen<256)
		return -ENOMEM;
	
	memset(buffer,0,buflen);

#ifdef CONFIG_KALLSYMS
	if (!kallsyms_address_to_symbol(address,&mod_name,&mod_start,&mod_end,&sec_name,
		&sec_start, &sec_end, &sym_name, &sym_start, &sym_end)) {
		/* kallsyms doesn't have a clue; lets try harder */
		bestsofar = 0;
		snprintf(buffer,buflen-1,"[unresolved]");
		
		this_mod = module_list;

		while (this_mod != NULL) {
			int i;
			/* walk the symbol list of this module. Only symbols
			   who's address is smaller than the searched for address
			   are relevant; and only if it's better than the best so far */
			for (i=0; i< this_mod->nsyms; i++)
				if ((this_mod->syms[i].value<=address) &&
					(bestsofar<this_mod->syms[i].value)) {
					snprintf(buffer,buflen-1,"%s [%s] 0x%x",
						this_mod->syms[i].name,
						this_mod->name,
						(unsigned int)(address - this_mod->syms[i].value));
					bestsofar = this_mod->syms[i].value;
				}
			this_mod = this_mod->next;
		}

	} else { /* kallsyms success */
		snprintf(buffer,buflen-1,"%s [%s] 0x%x",sym_name,mod_name,(unsigned int)(address-sym_start));
	}
#endif
	return strlen(buffer);
}

static char modlist[4096];
/* this function isn't smp safe but that's not really a problem; it's called from
 * oops context only and any locking could actually prevent the oops from going out;
 * the line that is generated is informational only and should NEVER prevent the real oops
 * from going out. 
 */
void print_modules(void)
{
	struct module *this_mod;
	int pos = 0, i;
	memset(modlist,0,4096);

#ifdef CONFIG_KALLSYMS
	this_mod = module_list;
	while (this_mod != NULL) {
		if (this_mod->name != NULL)
			pos +=snprintf(modlist+pos,160-pos-1,"%s ",this_mod->name);
		this_mod = this_mod->next;
	}
	printk("%s\n",modlist);
#endif
}
