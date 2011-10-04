/* Copyright (c) 2001 Red Hat, Inc. All rights reserved.
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Arjan van de Ven <arjanv@redhat.com
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>      /* for module_init/exit */
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>

#include <asm/e820.h>

extern struct e820map e820;
struct proc_dir_entry *e820_proc_entry;

static int e820_proc_output(char *buffer, int bufsize)
{
        int i,bufpos=0;

        for (i = 0; i < e820.nr_map; i++) {
		/* FIXME: check for overflow */
                bufpos += sprintf(buffer+bufpos,"%016Lx @ %016Lx ", 
                        e820.map[i].size, e820.map[i].addr);
                switch (e820.map[i].type) {
                case E820_RAM:  bufpos += sprintf(buffer+bufpos,"(usable)\n");
                                break;
                case E820_RESERVED:
                                bufpos += sprintf(buffer+bufpos,"(reserved)\n");
                                break;
                case E820_ACPI:
                                bufpos += sprintf(buffer+bufpos,"(ACPI data)\n");
                                break;
                case E820_NVS:
                                bufpos += sprintf(buffer+bufpos,"(ACPI NVS)\n");
                                break;
                default:        bufpos += sprintf(buffer+bufpos,"type %lu\n", e820.map[i].type);
                                break;
                }
        }
	return bufpos;
}






static int e820_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
        int len = e820_proc_output (page,4096);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

int e820_module_init(void)
{        
        /* /proc/e820info probably isn't the best place for it, need
           to find a better one */
	e820_proc_entry = create_proc_entry ("e820info", 0, NULL);
	if (e820_proc_entry==NULL)
		return -EIO;

	e820_proc_entry->read_proc = e820_read_proc;
	e820_proc_entry->owner = THIS_MODULE;

	return 0;
}


void e820_module_exit(void)
{
	 remove_proc_entry ("e820info", e820_proc_entry);
}

module_init(e820_module_init);
module_exit(e820_module_exit);

