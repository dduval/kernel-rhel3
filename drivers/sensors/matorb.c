/*
    matorb.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
    and Philip Edelbrock <phil@netroedge.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#define DEBUG 1

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/sensors.h>
#include <linux/sensors.h>
#include <linux/init.h>

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)) || \
    (LINUX_VERSION_CODE == KERNEL_VERSION(2,3,0))
#define init_MUTEX(s) do { *(s) = MUTEX; } while(0)
#endif


#ifndef THIS_MODULE
#define THIS_MODULE NULL
#endif

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2E, SENSORS_I2C_END };
static unsigned short normal_i2c_range[] = { SENSORS_I2C_END };
static unsigned int normal_isa[] = { SENSORS_ISA_END };
static unsigned int normal_isa_range[] = { SENSORS_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_1(matorb);

/* Many MATORB constants specified below */


/* Each client has this additional data */
struct matorb_data {
	int sysctl_id;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

};

#ifdef MODULE
extern int init_module(void);
extern int cleanup_module(void);
#endif				/* MODULE */

#ifdef MODULE
static
#else
extern
#endif
int __init sensors_matorb_init(void);
static int __init matorb_cleanup(void);
static int matorb_attach_adapter(struct i2c_adapter *adapter);
static int matorb_detect(struct i2c_adapter *adapter, int address,
			 unsigned short flags, int kind);
static void matorb_init_client(struct i2c_client *client);
static int matorb_detach_client(struct i2c_client *client);
static int matorb_command(struct i2c_client *client, unsigned int cmd,
			  void *arg);
static void matorb_inc_use(struct i2c_client *client);
static void matorb_dec_use(struct i2c_client *client);
static int matorb_write_value(struct i2c_client *client, u8 reg,
			      u16 value);
static void matorb_disp(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void matorb_update_client(struct i2c_client *client);


/* This is the driver that will be inserted */
static struct i2c_driver matorb_driver = {
	/* name */ "Matrix Orbital LCD driver",
	/* id */ I2C_DRIVERID_MATORB,
	/* flags */ I2C_DF_NOTIFY,
	/* attach_adapter */ &matorb_attach_adapter,
	/* detach_client */ &matorb_detach_client,
	/* command */ &matorb_command,
	/* inc_use */ &matorb_inc_use,
	/* dec_use */ &matorb_dec_use
};

/* These files are created for each detected MATORB. This is just a template;
   though at first sight, you might think we could use a statically
   allocated list, we need some way to get back to the parent - which
   is done through one of the 'extra' fields which are initialized
   when a new copy is allocated. */
static ctl_table matorb_dir_table_template[] = {
	{MATORB_SYSCTL_DISP, "disp", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &matorb_disp},
	{0}
};

/* Used by init/cleanup */
static int __initdata matorb_initialized = 0;

static int matorb_id = 0;

int matorb_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_detect(adapter, &addr_data, matorb_detect);
}

/* This function is called by i2c_detect */
int matorb_detect(struct i2c_adapter *adapter, int address,
		  unsigned short flags, int kind)
{
	int i, cur;
	struct i2c_client *new_client;
	struct matorb_data *data;
	int err = 0;
	const char *type_name = "matorb";
	const char *client_name = "matorb";

	/* Make sure we aren't probing the ISA bus!! This is just a safety check
	   at this moment; i2c_detect really won't call us. */
#ifdef DEBUG
	if (i2c_is_isa_adapter(adapter)) {
		printk
		    ("matorb.o: matorb_detect called for an ISA bus adapter?!?\n");
		return 0;
	}
#endif

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		    goto ERROR0;


	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access matorb_{read,write}_value. */
	if (!(new_client = kmalloc(sizeof(struct i2c_client) +
				   sizeof(struct matorb_data),
				   GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}

	data = (struct matorb_data *) (new_client + 1);
	new_client->addr = address;
	new_client->data = data;
	new_client->adapter = adapter;
	new_client->driver = &matorb_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. It is lousy. */
	cur = i2c_smbus_write_byte_data(new_client, 0x0FE, 0x58);	/* clear screen */

	printk("matorb.o: debug detect 0x%X\n", cur);

	/* Fill in the remaining client fields and put it into the global list */
	strcpy(new_client->name, client_name);

	new_client->id = matorb_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR3;

	/* Register a new directory entry with module sensors */
	if ((i = i2c_register_entry(new_client, type_name,
					matorb_dir_table_template,
					THIS_MODULE)) < 0) {
		err = i;
		goto ERROR4;
	}
	data->sysctl_id = i;

	matorb_init_client(new_client);
	return 0;

/* OK, this is not exactly good programming practice, usually. But it is
   very code-efficient in this case. */

      ERROR4:
	i2c_detach_client(new_client);
      ERROR3:
	kfree(new_client);
      ERROR0:
	return err;
}

int matorb_detach_client(struct i2c_client *client)
{
	int err;

	i2c_deregister_entry(((struct matorb_data *) (client->data))->
				 sysctl_id);

	if ((err = i2c_detach_client(client))) {
		printk
		    ("matorb.o: Client deregistration failed, client not detached.\n");
		return err;
	}

	kfree(client);

	return 0;
}


/* No commands defined yet */
int matorb_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	return 0;
}

/* Nothing here yet */
void matorb_inc_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

/* Nothing here yet */
void matorb_dec_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

#if 0
/* All registers are word-sized, except for the configuration register.
   MATORB uses a high-byte first convention, which is exactly opposite to
   the usual practice. */
int matorb_read_value(struct i2c_client *client, u8 reg)
{
	return -1;		/* Doesn't support reads */
}
#endif

/* All registers are word-sized, except for the configuration register.
   MATORB uses a high-byte first convention, which is exactly opposite to
   the usual practice. */
int matorb_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (reg == 0) {
		return i2c_smbus_write_byte(client, value);
	} else {
		return i2c_smbus_write_byte_data(client, reg, value);
	}
}

void matorb_init_client(struct i2c_client *client)
{
	/* Initialize the MATORB chip */
}

void matorb_update_client(struct i2c_client *client)
{
	struct matorb_data *data = client->data;

	down(&data->update_lock);

	if ((jiffies - data->last_updated > HZ + HZ / 2) ||
	    (jiffies < data->last_updated) || !data->valid) {

#ifdef DEBUG
		printk("Starting matorb update\n");
#endif

/* nothing yet */
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);
}


void matorb_disp(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	int i;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		matorb_update_client(client);
		results[0] = 0;
		*nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		for (i = 1; i <= *nrels_mag; i++) {
			matorb_write_value(client, 0, results[i - 1]);
		}
	}
}

int __init sensors_matorb_init(void)
{
	int res;

	printk("matorb.o version %s (%s)\n", LM_VERSION, LM_DATE);
	matorb_initialized = 0;
	if ((res = i2c_add_driver(&matorb_driver))) {
		printk
		    ("matorb.o: Driver registration failed, module not inserted.\n");
		matorb_cleanup();
		return res;
	}
	matorb_initialized++;
	return 0;
}

int __init matorb_cleanup(void)
{
	int res;

	if (matorb_initialized >= 1) {
		if ((res = i2c_del_driver(&matorb_driver))) {
			printk
			    ("matorb.o: Driver deregistration failed, module not removed.\n");
			return res;
		}
		matorb_initialized--;
	}

	return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE

MODULE_AUTHOR
    ("Frodo Looijaard <frodol@dds.nl> and Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("MATORB driver");

int init_module(void)
{
	return sensors_matorb_init();
}

int cleanup_module(void)
{
	return matorb_cleanup();
}

#endif				/* MODULE */
