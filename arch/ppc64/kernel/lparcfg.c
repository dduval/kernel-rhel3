/*
 * PowerPC64 LPAR Configuration Information Driver
 *
 * Dave Engebretsen engebret@us.ibm.com
 *    Copyright (c) 2003 Dave Engebretsen
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * This driver creates a proc file at /proc/ppc64/lparcfg which contains
 * keyword - value pairs that specify the configuration of the partition.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/ItLpPaca.h>

#define MODULE_VERSION "1.0"
#define MODULE_NAME "lparcfg"

static struct proc_dir_entry *proc_ppc64_lparcfg;
#define LPARCFG_BUFF_SIZE 4096

static DECLARE_MUTEX(lparcfg_sem);

#ifdef CONFIG_PPC_ISERIES
extern unsigned char e2a(unsigned char);

/* 
 * Methods used to fetch LPAR data when running on an iSeries platform.
 */
static void lparcfg_data(unsigned char *buf, unsigned long size) 
{
	unsigned long n = 0, pool_id, lp_index; 
	int shared, entitled_capacity, max_entitled_capacity;
	int processors, max_processors;
	struct paca_struct *lpaca = get_paca();

	memset(buf, 0, size); 

	shared = (int)(lpaca->xLpPacaPtr->xSharedProc);
	n += snprintf(buf, LPARCFG_BUFF_SIZE - n,
		      "serial_number=%c%c%c%c%c%c%c\n", 
		      e2a(xItExtVpdPanel.mfgID[2]),
		      e2a(xItExtVpdPanel.mfgID[3]),
		      e2a(xItExtVpdPanel.systemSerial[1]),
		      e2a(xItExtVpdPanel.systemSerial[2]),
		      e2a(xItExtVpdPanel.systemSerial[3]),
		      e2a(xItExtVpdPanel.systemSerial[4]),
		      e2a(xItExtVpdPanel.systemSerial[5])); 

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_type=%c%c%c%c\n",
		      e2a(xItExtVpdPanel.machineType[0]),
		      e2a(xItExtVpdPanel.machineType[1]),
		      e2a(xItExtVpdPanel.machineType[2]),
		      e2a(xItExtVpdPanel.machineType[3])); 

	lp_index = HvLpConfig_getLpIndex(); 
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_id=%d\n", (int)lp_index); 

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_active_processors=%d\n", 
		      (int)HvLpConfig_getSystemPhysicalProcessors()); 

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_potential_processors=%d\n", 
		      (int)HvLpConfig_getSystemPhysicalProcessors()); 

	processors = (int)HvLpConfig_getPhysicalProcessors(); 
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_active_processors=%d\n", processors);  

	max_processors = (int)HvLpConfig_getMaxPhysicalProcessors(); 
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_potential_processors=%d\n", max_processors);  

	if(shared) {
		entitled_capacity = HvLpConfig_getSharedProcUnits(); 
		max_entitled_capacity = HvLpConfig_getMaxSharedProcUnits(); 
	} else {
		entitled_capacity = processors * 100; 
		max_entitled_capacity = max_processors * 100; 
	}
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_entitled_capacity=%d\n", entitled_capacity);

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_max_entitled_capacity=%d\n", 
		      max_entitled_capacity);

	if(shared) {
		pool_id = HvLpConfig_getSharedPoolIndex(); 
		n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, "pool=%d\n", 
			      (int)pool_id); 
		n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
			      "pool_capacity=%d\n", 
		     (int)(HvLpConfig_getNumProcsInSharedPool(pool_id)*100)); 
	}

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "shared_processor_mode=%d\n", shared);
}
#endif /* CONFIG_PPC_ISERIES */

#ifdef CONFIG_PPC_PSERIES
/* 
 * Methods used to fetch LPAR data when running on a pSeries platform.
 */
static void lparcfg_data(unsigned char *buf, unsigned long size) 
{
	unsigned long n = 0;
	int shared, entitled_capacity, max_entitled_capacity;
	int processors, system_active_processors; 
	struct device_node *root;
        const char *model = "";
        const char *system_id = "";
	unsigned int *lp_index_ptr, lp_index = 0;
        struct device_node *rtas_node;
        int *ip;

	memset(buf, 0, size); 

	root = find_path_device("/");
        if (root) {
                model = get_property(root, "model", NULL);
                system_id = get_property(root, "system-id", NULL);
                lp_index_ptr = (unsigned int *)get_property(root, "ibm,partition-no", NULL);
		if(lp_index_ptr) lp_index = *lp_index_ptr;
	}

	n  = snprintf(buf, LPARCFG_BUFF_SIZE - n,
		      "serial_number=%s\n", system_id); 

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_type=%s\n", model); 

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_id=%d\n", (int)lp_index); 

        rtas_node = find_path_device("/rtas");
        ip = (int *)get_property(rtas_node, "ibm,lrdr-capacity", NULL);
        if (ip == NULL) {
		system_active_processors = systemcfg->processorCount; 
        } else {
		system_active_processors = *(ip + 4);
	}
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_active_processors=%d\n", 
		      system_active_processors);

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "system_potential_processors=%d\n", 
		      system_active_processors);

	processors = systemcfg->processorCount;
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_active_processors=%d\n", processors);  

	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_potential_processors=%d\n",
		      system_active_processors);

	entitled_capacity = processors * 100; 
	max_entitled_capacity = system_active_processors * 100; 
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_entitled_capacity=%d\n", entitled_capacity);
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "partition_max_entitled_capacity=%d\n", 
		      max_entitled_capacity);

	shared = 0;
	n += snprintf(buf+n, LPARCFG_BUFF_SIZE - n, 
		      "shared_processor_mode=%d\n", shared);
}
#endif /* CONFIG_PPC_PSERIES */

static ssize_t lparcfg_read(struct file *file, char *buf,
			    size_t count, loff_t *ppos)
{
	struct proc_dir_entry *dp = file->f_dentry->d_inode->u.generic_ip;
	unsigned long *data = (unsigned long *)dp->data;
	unsigned long p;
	ssize_t ret;
	char * pnt;

	if (!data) {
		printk(KERN_ERR "lparcfg: read failed no data\n");
		return -EIO;
	}
	p = *ppos;
	if (p >= LPARCFG_BUFF_SIZE) 
		return 0;

	if (down_interruptible(&lparcfg_sem))
		return -ERESTARTSYS;

	lparcfg_data((unsigned char *)data, LPARCFG_BUFF_SIZE); 

	if (count > (strlen((char *)data) - p))
		count = (strlen((char *)data)) - p;
	pnt = (char *)(data) + p;
	if (copy_to_user(buf, (void *)pnt, count))
		ret = -EFAULT;
	else {
		ret = count;
		*ppos += count;
	}
	up(&lparcfg_sem);
	return ret;
}

static int lparcfg_open(struct inode * inode, struct file * file)
{
	struct proc_dir_entry *dp = file->f_dentry->d_inode->u.generic_ip;
	unsigned int *data = (unsigned int *)dp->data;

	if (!data) {
		printk(KERN_ERR "lparcfg: open failed no data\n");
		return -EIO;
	}

	return 0;
}

struct file_operations lparcfg_fops = {
	owner:		THIS_MODULE,
	read:		lparcfg_read,
	open:		lparcfg_open,
};

int __init lparcfg_init(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("ppc64/lparcfg", S_IRUSR, NULL);
	if (ent) {
		ent->proc_fops = &lparcfg_fops;
		ent->data = kmalloc(LPARCFG_BUFF_SIZE, GFP_KERNEL);
		if (!ent->data) {
			printk(KERN_ERR "Failed to allocate buffer for lparcfg\n");
			remove_proc_entry("lparcfg", ent->parent);
			return -ENOMEM;
		}
	} else {
		printk(KERN_ERR "Failed to create ppc64/lparcfg\n");
		return -EIO;
	}

	proc_ppc64_lparcfg = ent;
	return 0;
}

void __exit lparcfg_cleanup(void)
{
	if (proc_ppc64_lparcfg) {
		if (proc_ppc64_lparcfg->data) {
		    kfree(proc_ppc64_lparcfg->data);
		}
		remove_proc_entry("lparcfg", proc_ppc64_lparcfg->parent);
	}
}

module_init(lparcfg_init);
module_exit(lparcfg_cleanup);
MODULE_DESCRIPTION("Interface for LPAR configuration data");
MODULE_AUTHOR("Dave Engebretsen");
MODULE_LICENSE("GPL");
