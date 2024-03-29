/*
    LM87.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 2000  Frodo Looijaard <frodol@dds.nl>
                        Philip Edelbrock <phil@netroedge.com>
			Stephen Rousset <stephen.rousset@rocketlogix.com>
			Dan Eaton <dan.eaton@rocketlogix.com>

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


#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/sysctl.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/sensors.h>
#include <linux/sensors.h>
#include <linux/init.h>

/* Chip configuration settings.  These should be set to reflect the
HARDWARE configuration of your chip.  By default (read: when all of
these are left commented out), this driver assumes that the
configuration is the same as National's defaults for the Channel Mode
register.

Set to '1' the appropriate defines, as nessesary:

 - External temp sensors 2 (possible second CPU temp)
   This will disable the 2.5V and Vccp2 readings.
   Ironically, National decided that you can read the
   temperature of a second CPU or it's core voltage,
   but not both!  Comment out if FAULT is reported.  */

/* #define LM87_EXT2 1 */

/* Aux analog input. When enabled, the Fan 1 reading 
   will be disabled */

/* #define LM87_AIN1 1 */

/* Aux analog input 2. When enabled, the Fan 2 reading 
   will be disabled */

/* #define LM87_AIN2 1 */

/* Internal Vcc is 5V instead of 3.3V */

/* #define LM87_5V_VCC 1 */

/* That's the end of the hardware config defines.  I would have made
   them insmod params, but it would be too much work. ;') */


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)) || \
    (LINUX_VERSION_CODE == KERNEL_VERSION(2,3,0))
#define init_MUTEX(s) do { *(s) = MUTEX; } while(0)
#endif

#ifndef THIS_MODULE
#define THIS_MODULE NULL
#endif

/* Addresses to scan */
static unsigned short normal_i2c[] = { SENSORS_I2C_END };
static unsigned short normal_i2c_range[] = { 0x2c, 0x2e, SENSORS_I2C_END };
static unsigned int normal_isa[] = { SENSORS_ISA_END };
static unsigned int normal_isa_range[] = { SENSORS_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_1(lm87);

/* The following is the calculation for the register offset
 * for the monitored items minimum and maximum locations.
 */
#define LM87_REG_IN_MAX(nr) (0x2b + ((nr) * 2))
#define LM87_REG_IN_MIN(nr) (0x2c + ((nr) * 2))
#define LM87_REG_IN(nr) (0x20 + (nr))

/* Initial limits */

/*
 * LM87 register definition
 * 
 */

      /* The LM87 registers */
#define LM87_INT_TEMP_HI_LIMIT_LOCKABLE  0x13
#define LM87_EXT_TEMP_HI_LIMIT_LOCKABLE  0x14
#define LM87_REG_TEST                    0x15
#define LM87_REG_CHANNEL_MODE            0x16
#define LM87_REG_INT_TEMP_HI_LIMIT       0x17
#define LM87_REG_EXT_TEMP_HI_LIMIT       0x18
#define LM87_REG_ANALOG_OUT              0x19

      /* These are all read-only */
#define LM87_REG_2_5V_EXT_TEMP_2         0x20
#define LM87_REG_VCCP1                   0x21
#define LM87_REG_3_3V                    0x22  
#define LM87_REG_5V                      0x23
#define LM87_REG_12V                     0x24
#define LM87_REG_VCCP2                   0x25
#define LM87_REG_EXT_TEMP_1              0x26
#define LM87_REG_INT_TEMP                0x27  /* LM87 temp. */
#define LM87_REG_FAN1_AIN1               0x28
#define LM87_REG_FAN2_AIN2               0x29

/* These are read/write */
#define LM87_REG_AIN1_LOW                0x1A
#define LM87_REG_AIN2_LOW                0x1B
#define LM87_REG_2_5V_EXT_TEMP_2_HIGH    0x2B  
#define LM87_REG_2_5V_EXT_TEMP_2_LOW     0x2C  
#define LM87_REG_VCCP1_HIGH              0x2D  
#define LM87_REG_VCCP1_LOW               0x2E  
#define LM87_REG_3_3V_HIGH               0x2F
#define LM87_REG_3_3V_LOW                0x30
#define LM87_REG_5V_HIGH                 0x31
#define LM87_REG_5V_LOW                  0x32
#define LM87_REG_12V_HIGH                0x33
#define LM87_REG_12V_LOW                 0x34
#define LM87_REG_VCCP2_HIGH              0x35
#define LM87_REG_VCCP2_LOW               0x36
#define LM87_REG_EXT_TEMP_1_HIGH         0x37    
#define LM87_REG_EXT_TEMP_1_LOW          0x38  
#define LM87_REG_INT_TEMP_HIGH           0x39  
#define LM87_REG_INT_TEMP_LOW            0x3A  
#define LM87_REG_FAN1_AIN1_LIMIT         0x3B
#define LM87_REG_FAN2_AIN2_LIMIT         0x3C
#define LM87_REG_COMPANY_ID              0x3E 
#define LM87_REG_DIE_REV                 0x3F

#define LM87_REG_CONFIG                  0x40
#define LM87_REG_INT1_STAT               0x41
#define LM87_REG_INT2_STAT               0x42
#define LM87_REG_INT1_MASK               0x43
#define LM87_REG_INT2_MASK               0x44
#define LM87_REG_CHASSIS_CLEAR           0x46
#define LM87_REG_VID_FAN_DIV             0x47
#define LM87_REG_VID4                    0x49
#define LM87_REG_CONFIG_2                0x4A
#define LM87_REG_INTRPT_STATUS_1_MIRROR  0x4C
#define LM87_REG_INTRPT_STATUS_2_MIRROR  0x4D
#define LM87_REG_SMBALERT_NUM_ENABLE     0x80



/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */

#define IN_TO_REG(val,nr) (SENSORS_LIMIT(((val) & 0xff),0,255))
#define IN_FROM_REG(val,nr) (val)

extern inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1,
			     254);
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:\
                               (val)==255?0:1350000/((div)*(val)))

#define TEMP_FROM_REG(temp)  (temp * 10)

#define TEMP_LIMIT_FROM_REG(val) (((val)>0x80?(val)-0x100:(val))*10)

#define TEMP_LIMIT_TO_REG(val) SENSORS_LIMIT(((val)<0?(((val)-5)/10):\
                                                      ((val)+5)/10),0,255)
#if 0
#define TEMP_FROM_REG(temp) \
   ((temp)<256?((((temp)&0x1fe) >> 1) * 10)      + ((temp) & 1) * 5:  \
               ((((temp)&0x1fe) >> 1) -255) * 10 - ((temp) & 1) * 5)  \

#define TEMP_LIMIT_FROM_REG(val) (val)

#define TEMP_LIMIT_TO_REG(val) SENSORS_LIMIT((val),0,255)
#endif


#define ALARMS_FROM_REG(val) (val)

#define DIV_FROM_REG(val) (1 << (val))
#define DIV_TO_REG(val) ((val)==1?0:((val)==8?3:((val)==4?2:1)))

#define VID_FROM_REG(val) ((val)==0x1f?0:(val)>=0x10?510-(val)*10:\
                           205-(val)*5)
                           
#define LM87_INIT_FAN_MIN 3000

#define LM87_INIT_EXT_TEMP_MAX 600
#define LM87_INIT_EXT_TEMP_MIN 100
#define LM87_INIT_INT_TEMP_MAX 600
#define LM87_INIT_INT_TEMP_MIN 100

#ifdef MODULE
extern int init_module(void);
extern int cleanup_module(void);
#endif				/* MODULE */

/* For each registered LM87, we need to keep some data in memory. That
   data is pointed to by LM87_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new LM87 client is
   allocated. */
struct lm87_data {
	int sysctl_id;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8  in[6];		/* Scaled Register value */
	u8  in_max[6];		/* Scaled Register value */
	u8  in_min[6];		/* Scaled Register value */
	u8  ain1;		/* Register value */
	u8  ain1_min;		/* Register value */
	u8  ain1_max;		/* Register value */
	u8  ain2;		/* Register value */
	u8  ain2_min;		/* Register value */
	u8  ain2_max;		/* Register value */
	u8  fan;		/* Register value */
	u8  fan_min;		/* Register value */
	u8  fan_div;		/* Register encoding, shifted right */
	u8  fan2;		/* Register value */
	u8  fan2_min;		/* Register value */
	u8  fan2_div;		/* Register encoding, shifted right */
	int ext2_temp;		/* Temp, shifted right */
	int ext_temp;           /* Temp, shifted right */
	int int_temp;		/* Temp, shifted right */
	u8  ext_temp_max;       /* Register value */
	u8  ext_temp_min;       /* Register value */
	u8  ext2_temp_max; 	/* Register value */
	u8  ext2_temp_min;	/* Register value */
	u8  int_temp_max;       /* Register value */
	u8  int_temp_min;	/* Register value */
	u16 alarms;		/* Register encoding, combined */
	u8  analog_out;		/* Register value */
	u8  vid;		/* Register value combined */
};

#ifdef MODULE
static
#else
extern
#endif
int __init sensors_lm87_init(void);
static int __init lm87_cleanup(void);

static int lm87_attach_adapter(struct i2c_adapter *adapter);
static int lm87_detect(struct i2c_adapter *adapter, int address,
			  unsigned short flags, int kind);
static int lm87_detach_client(struct i2c_client *client);
static int lm87_command(struct i2c_client *client, unsigned int cmd,
			   void *arg);
static void lm87_inc_use(struct i2c_client *client);
static void lm87_dec_use(struct i2c_client *client);

static int lm87_read_value(struct i2c_client *client, u8 register);
static int lm87_write_value(struct i2c_client *client, u8 register,
			       u8 value);
static void lm87_update_client(struct i2c_client *client);
static void lm87_init_client(struct i2c_client *client);


static void lm87_in(struct i2c_client *client, int operation,
		       int ctl_name, int *nrels_mag, long *results);
#if defined (LM87_AIN1) || defined (LM87_AIN2)
static void lm87_ain(struct i2c_client *client, int operation,
		       int ctl_name, int *nrels_mag, long *results);
#endif
static void lm87_fan(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void lm87_temp(struct i2c_client *client, int operation,
			 int ctl_name, int *nrels_mag, long *results);
static void lm87_alarms(struct i2c_client *client, int operation,
			   int ctl_name, int *nrels_mag, long *results);
static void lm87_fan_div(struct i2c_client *client, int operation,
			    int ctl_name, int *nrels_mag, long *results);
static void lm87_analog_out(struct i2c_client *client, int operation,
			       int ctl_name, int *nrels_mag,
			       long *results);
static void lm87_vid(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);

/* I choose here for semi-static LM87 allocation. Complete dynamic
   allocation could also be used; the code needed for this would probably
   take more memory than the datastructure takes now. */
static int lm87_id = 0;

static struct i2c_driver LM87_driver = {
	/* name */          "LM87 sensor driver",
	/* id */             I2C_DRIVERID_LM87,
	/* flags */          I2C_DF_NOTIFY,
	/* attach_adapter */ &lm87_attach_adapter,
	/* detach_client */  &lm87_detach_client,
	/* command */        &lm87_command,
	/* inc_use */        &lm87_inc_use,
	/* dec_use */        &lm87_dec_use
};

/* Used by LM87_init/cleanup */
static int __initdata lm87_initialized = 0;

/* The /proc/sys entries */
/* These files are created for each detected LM87. This is just a template;
   though at first sight, you might think we could use a statically
   allocated list, we need some way to get back to the parent - which
   is done through one of the 'extra' fields which are initialized 
   when a new copy is allocated. */

static ctl_table LM87_dir_table_template[] = {
#ifdef LM87_AIN1
	{LM87_SYSCTL_AIN1, "in6", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_ain},
#endif
#ifdef LM87_AIN2
	{LM87_SYSCTL_AIN2, "in7", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_ain},
#endif
#ifndef LM87_EXT2
	{LM87_SYSCTL_IN0, "in0", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
	{LM87_SYSCTL_IN5, "in5", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
#endif
	{LM87_SYSCTL_IN1, "in1", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
	{LM87_SYSCTL_IN2, "in2", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
	{LM87_SYSCTL_IN3, "in3", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
	{LM87_SYSCTL_IN4, "in4", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_in},
#ifndef LM87_AIN1
	{LM87_SYSCTL_FAN1, "fan1", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_fan},
	{LM87_SYSCTL_FAN_DIV, "fan_div", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_fan_div},
#define LM87_FANDIV_FLAG
#endif
#ifndef LM87_AIN2
	{LM87_SYSCTL_FAN2, "fan2", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_fan},
#ifndef LM87_FANDIV_FLAG
	{LM87_SYSCTL_FAN_DIV, "fan_div", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_fan_div},
#endif /* LM87_FANDIV_FLAG */
#endif /* LM87_AIN2 */
#ifdef LM87_EXT2
        {LM87_SYSCTL_TEMP3, "temp3", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_temp},
#endif
	{LM87_SYSCTL_TEMP2, "temp2", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_temp},
	{LM87_SYSCTL_TEMP1, "temp1", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_temp},
	{LM87_SYSCTL_ALARMS, "alarms", NULL, 0, 0444, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_alarms},
	{LM87_SYSCTL_ANALOG_OUT, "analog_out", NULL, 0, 0644, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_analog_out},
	{LM87_SYSCTL_VID, "vid", NULL, 0, 0444, NULL, &i2c_proc_real,
	  &i2c_sysctl_real, NULL, &lm87_vid},
	{0}
};

int lm87_attach_adapter(struct i2c_adapter *adapter)
{
	int error;
	struct i2c_client_address_data  lm87_client_data;

	lm87_client_data.normal_i2c       = addr_data.normal_i2c;
	lm87_client_data.normal_i2c_range = addr_data.normal_i2c_range;
	lm87_client_data.probe            = addr_data.probe;
	lm87_client_data.probe_range      = addr_data.probe_range;
	lm87_client_data.ignore           = addr_data.ignore;
	lm87_client_data.ignore_range     = addr_data.ignore_range;
	lm87_client_data.force            = addr_data.forces->force;

	error = i2c_probe(adapter, &lm87_client_data, lm87_detect);
	i2c_detect(adapter, &addr_data, lm87_detect);

        return error;
}

static int lm87_detect(struct i2c_adapter *adapter, int address,
			  unsigned short flags, int kind)
{
	int i;
	struct i2c_client *new_client;
	struct lm87_data *data;
	int err = 0;
	const char *type_name = "";
	const char *client_name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto ERROR0;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access LM87_{read,write}_value. */

	if (!(new_client = kmalloc(sizeof(struct i2c_client) +
				   sizeof(struct lm87_data),
				   GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}

	data = (struct lm87_data *) (new_client + 1);
	new_client->addr = address;
	new_client->data = data;
	new_client->adapter = adapter;
	new_client->driver = &LM87_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	if (kind < 0) {
		if (((lm87_read_value(new_client, LM87_REG_CONFIG) & 0x80)
		     != 0x00) ||
		    (lm87_read_value(new_client, LM87_REG_COMPANY_ID) != 0x02))
	       goto ERROR1;
	}

	/* Fill in the remaining client fields and put into the global list */
        type_name = "lm87";
        client_name = "LM87 chip";
	strcpy(new_client->name, client_name);
	data->type = kind;

	new_client->id = lm87_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR3;

	/* Register a new directory entry with module sensors */
	if ((i = i2c_register_entry(new_client,
					type_name,
					LM87_dir_table_template,
					THIS_MODULE)) < 0) {
		err = i;
		goto ERROR4;
	}
	data->sysctl_id = i;

	/* Initialize the LM87 chip */
	lm87_init_client(new_client);
	return 0;

/* OK, this is not exactly good programming practice, usually. But it is
   very code-efficient in this case. */

      ERROR4:
	i2c_detach_client(new_client);
      ERROR3:
      ERROR1:
	kfree(new_client);
      ERROR0:
	return err;
}

int lm87_detach_client(struct i2c_client *client)
{
	int err;

	i2c_deregister_entry(((struct lm87_data *) (client->data))->
				 sysctl_id);

	if ((err = i2c_detach_client(client))) {
		printk
		    ("lm87.o: Client deregistration failed, client not detached.\n");
		return err;
	}

	kfree(client);

	return 0;

}

/* No commands defined yet */
int lm87_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	return 0;
}

void lm87_inc_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void lm87_dec_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

int lm87_read_value(struct i2c_client *client, u8 reg)
{
	return 0xFF & i2c_smbus_read_byte_data(client, reg);
}

int lm87_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new LM87. It should set limits, etc. */
void lm87_init_client(struct i2c_client *client)
{
	int vid;
	u8 v;

	/* Reset all except Watchdog values and last conversion values
	   This sets fan-divs to 2, among others. This makes most other
	   initializations unnecessary */
	lm87_write_value(client, LM87_REG_CONFIG, 0x80);

        /* Setup Channel Mode register for configuration of monitoring 
	 * Default is 00000000b
	 * 	bit 0 - Configures Fan 1/AIN 1 input (1 = AIN)
	 * 	bit 1 - Configures Fan 2/AIN 2 input (1 = AIN)
	 * 	bit 2 - Configures 2.5V&Vccp2/D2 input (1 = 2nd Therm.) 
	 * 	bit 3 - Configures Vcc for 5V/3.3V reading (0 = 3.3V)
	 * 	bit 4 - Configures IRQ0 Enable if = 1
	 * 	bit 5 - Configures IRQ1 Enable if = 1
	 * 	bit 6 - Configures IRQ2 Enable if = 1
	 * 	bit 7 - Configures VID/IRQ input as interrupts if = 1
	 */

/* I know, not clean, but it works. :'p */
	lm87_write_value(client, LM87_REG_CHANNEL_MODE,
#ifdef LM87_AIN1
 0x01
#else
0
#endif
 | 
#ifdef LM87_AIN2
 0x02
#else
0
#endif
 |
#ifdef LM87_EXT2
 0x04
#else
0
#endif
 | 
#ifdef LM87_5V_VCC
0x08
#else   
0
#endif
	);

	/* Set IN (voltage) initial limits to sane values  +/- 5% */
	lm87_write_value(client, LM87_REG_IN_MIN(0),182);
	lm87_write_value(client, LM87_REG_IN_MAX(0),202);
	lm87_write_value(client, LM87_REG_IN_MIN(2),182);
	lm87_write_value(client, LM87_REG_IN_MAX(2),202);
	lm87_write_value(client, LM87_REG_IN_MIN(3),182);
	lm87_write_value(client, LM87_REG_IN_MAX(3),202);
	lm87_write_value(client, LM87_REG_IN_MIN(4),182);
	lm87_write_value(client, LM87_REG_IN_MAX(4),202);

	/* Set CPU core voltage limits relative to vid readings +/- 5% */
	v = (lm87_read_value(client, LM87_REG_VID_FAN_DIV) & 0x0f)
		    | ((lm87_read_value(client, LM87_REG_VID4) & 0x01)
                    << 4 );
	vid = VID_FROM_REG(v);
	v = vid * 95 * 192 / 27000;
	lm87_write_value(client, LM87_REG_IN_MIN(1), v);
	lm87_write_value(client, LM87_REG_IN_MIN(5), v);
	v = vid * 105 * 192 / 27000;
	lm87_write_value(client, LM87_REG_IN_MAX(1), v);
	lm87_write_value(client, LM87_REG_IN_MAX(5), v);

	/* Set Temp initial limits to sane values */
	lm87_write_value(client, LM87_REG_EXT_TEMP_1_HIGH,
			    TEMP_LIMIT_TO_REG(LM87_INIT_EXT_TEMP_MAX));
	lm87_write_value(client, LM87_REG_EXT_TEMP_1_LOW,
			    TEMP_LIMIT_TO_REG(LM87_INIT_EXT_TEMP_MIN));
#ifdef LM87_EXT2
	lm87_write_value(client, LM87_REG_2_5V_EXT_TEMP_2_HIGH,
			    TEMP_LIMIT_TO_REG(LM87_INIT_EXT_TEMP_MAX));
	lm87_write_value(client, LM87_REG_2_5V_EXT_TEMP_2_LOW,
			    TEMP_LIMIT_TO_REG(LM87_INIT_EXT_TEMP_MIN));
#endif
	lm87_write_value(client, LM87_REG_INT_TEMP_HIGH,
			    TEMP_LIMIT_TO_REG(LM87_INIT_INT_TEMP_MAX));
	lm87_write_value(client, LM87_REG_INT_TEMP_LOW,
			    TEMP_LIMIT_TO_REG(LM87_INIT_INT_TEMP_MIN));

#ifndef LM87_AIN1
	lm87_write_value(client, LM87_REG_FAN1_AIN1_LIMIT,
			    FAN_TO_REG(LM87_INIT_FAN_MIN, 2));
#endif
#ifndef LM87_AIN2
	lm87_write_value(client, LM87_REG_FAN2_AIN2_LIMIT,
			    FAN_TO_REG(LM87_INIT_FAN_MIN, 2));
#endif

	/* Start monitoring */
	lm87_write_value(client, LM87_REG_CONFIG, 0x01);
}

void lm87_update_client(struct i2c_client *client)
{
	struct lm87_data *data = client->data;
	int i;

	down(&data->update_lock);

	if ((jiffies - data->last_updated > HZ) ||  /* 1 sec cache */
            (jiffies < data->last_updated)      || 
             !data->valid) {
		for (i = 0; i <= 5; i++) {  
		 data->in[i] = 
		    lm87_read_value(client,LM87_REG_IN(i));
		 data->in_min[i] = 
		    lm87_read_value(client,LM87_REG_IN_MIN(i));
		 data->in_max[i] = 
		    lm87_read_value(client,LM87_REG_IN_MAX(i));
		}
		 data->ain1 = 
		    lm87_read_value(client,LM87_REG_FAN1_AIN1);
		 data->ain1_min =
		    lm87_read_value(client,LM87_REG_AIN1_LOW);
		 data->ain1_max =
		    lm87_read_value(client,LM87_REG_FAN1_AIN1_LIMIT);
		 data->ain2 = 
		    lm87_read_value(client,LM87_REG_FAN2_AIN2);
		 data->ain2_min =
		    lm87_read_value(client,LM87_REG_AIN2_LOW);
		 data->ain2_max =
		    lm87_read_value(client,LM87_REG_FAN2_AIN2_LIMIT);

		data->fan =
		    lm87_read_value(client, LM87_REG_FAN1_AIN1);
		data->fan_min =
		    lm87_read_value(client, LM87_REG_FAN1_AIN1_LIMIT);
		data->fan2 =
		    lm87_read_value(client, LM87_REG_FAN2_AIN2);
		data->fan2_min =
		    lm87_read_value(client, LM87_REG_FAN2_AIN2_LIMIT);

		data->ext2_temp =
		    lm87_read_value(client, LM87_REG_2_5V_EXT_TEMP_2);
		data->ext_temp =
		    lm87_read_value(client, LM87_REG_EXT_TEMP_1);
		data->int_temp =
		    lm87_read_value(client, LM87_REG_INT_TEMP);

		data->ext2_temp_max =
		    lm87_read_value(client, LM87_REG_2_5V_EXT_TEMP_2_HIGH);
		data->ext2_temp_min =
		    lm87_read_value(client, LM87_REG_2_5V_EXT_TEMP_2_LOW);

		data->ext_temp_max =
		    lm87_read_value(client, LM87_REG_EXT_TEMP_1_HIGH);
		data->ext_temp_min =
		    lm87_read_value(client, LM87_REG_EXT_TEMP_1_LOW);

		data->int_temp_max =
		    lm87_read_value(client, LM87_REG_INT_TEMP_HIGH);
		data->int_temp_min =
		    lm87_read_value(client, LM87_REG_INT_TEMP_LOW);

		i = lm87_read_value(client, LM87_REG_VID_FAN_DIV);
		data->fan_div = (i >> 4) & 0x03;
		data->fan2_div = (i >> 6) & 0x03;
		data->vid = i & 0x0f;
		data->vid |=
		    (lm87_read_value(client, LM87_REG_VID4) & 0x01)
		    << 4;
		data->alarms =
		    lm87_read_value(client, LM87_REG_INT1_STAT) +
		    (lm87_read_value(client, LM87_REG_INT2_STAT) << 8);
		data->analog_out =
		    lm87_read_value(client, LM87_REG_ANALOG_OUT);
		data->last_updated = jiffies;
		data->valid = 1;
	}
	up(&data->update_lock);
}


/* The next few functions are the call-back functions of the /proc/sys and
   sysctl files. Which function is used is defined in the ctl_table in
   the extra1 field.
   Each function must return the magnitude (power of 10 to divide the date
   with) if it is called with operation==SENSORS_PROC_REAL_INFO. It must
   put a maximum of *nrels elements in results reflecting the data of this
   file, and set *nrels to the number it actually put in it, if operation==
   SENSORS_PROC_REAL_READ. Finally, it must get upto *nrels elements from
   results and write them to the chip, if operations==SENSORS_PROC_REAL_WRITE.
   Note that on SENSORS_PROC_REAL_READ, I do not check whether results is
   large enough (by checking the incoming value of *nrels). This is not very
   good practice, but as long as you put less than about 5 values in results,
   you can assume it is large enough. */
void lm87_in(struct i2c_client *client, int operation, int ctl_name,
		int *nrels_mag, long *results)
{
	long scales[6] = { 250, 270, 
#ifdef LM87_5V_VCC
500,
#else
330,
#endif
		500, 1200, 270 };

	struct lm87_data *data = client->data;
	int nr = ctl_name - LM87_SYSCTL_IN0;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 2;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		results[0] =
		    ((long)data->in_min[nr] * scales[nr]) / 192;
		results[1] =
		    ((long)data->in_max[nr] * scales[nr]) / 192;
		results[2] =
		    ((long)data->in[nr] * scales[nr]) / 192;
		*nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->in_min[nr] =
			    (results[0] * 192) / scales[nr];
			lm87_write_value(client, LM87_REG_IN_MIN(nr),
					    data->in_min[nr]);
		}
		if (*nrels_mag >= 2) {
			data->in_max[nr] =
			    (results[1] * 192) / scales[nr];
			lm87_write_value(client, LM87_REG_IN_MAX(nr),
					    data->in_max[nr]);
		}
	}
}

#if defined (LM87_AIN1) || defined (LM87_AIN2)
void lm87_ain(struct i2c_client *client, int operation, int ctl_name,
		int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		if (ctl_name == LM87_SYSCTL_AIN1) {
		 results[0] = data->ain1_min;
		 results[1] = data->ain1_max;
		 results[2] = data->ain1;
		} else {
		 results[0] = data->ain2_min;
		 results[1] = data->ain2_max;
		 results[2] = data->ain2;
		}
		*nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
		 if (ctl_name == LM87_SYSCTL_AIN1) {
			data->ain1_min = results[0];
			lm87_write_value(client, LM87_REG_AIN1_LOW,
					    data->ain1_min);
		 } else {
			data->ain2_min = results[0];
			lm87_write_value(client, LM87_REG_AIN2_LOW,
					    data->ain2_min);
		 }
		}
		if (*nrels_mag >= 2) {
		 if (ctl_name == LM87_SYSCTL_AIN1) {
			data->ain1_max = results[1];
			lm87_write_value(client, LM87_REG_FAN1_AIN1_LIMIT,
					    data->ain1_max);
		 } else {
			data->ain2_max = results[1];
			lm87_write_value(client, LM87_REG_FAN2_AIN2_LIMIT,
					    data->ain2_max);
		 }
		}
	}
}
#endif

void lm87_fan(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		if (ctl_name == LM87_SYSCTL_FAN1) {
		 results[0] = FAN_FROM_REG(data->fan_min,
					  DIV_FROM_REG(data->fan_div));
		 results[1] = FAN_FROM_REG(data->fan, 
		                         DIV_FROM_REG(data->fan_div));
		} else {
		 results[0] = FAN_FROM_REG(data->fan2_min,
					  DIV_FROM_REG(data->fan2_div));
		 results[1] = FAN_FROM_REG(data->fan2, 
		                         DIV_FROM_REG(data->fan2_div));
		}
		*nrels_mag = 2;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 0) {
			if (ctl_name == LM87_SYSCTL_FAN1) {
			 data->fan_min = FAN_TO_REG(results[0],
						   DIV_FROM_REG
						   (data->fan_div));
			 lm87_write_value(client, LM87_REG_FAN1_AIN1_LIMIT,
					    data->fan_min);
			} else {
			 data->fan2_min = FAN_TO_REG(results[0],
						   DIV_FROM_REG
						   (data->fan2_div));
			 lm87_write_value(client, LM87_REG_FAN2_AIN2_LIMIT,
					    data->fan2_min);
			}
		}
	}
}


void lm87_temp(struct i2c_client *client, int operation, int ctl_name,
		  int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 1;
	else if (operation == SENSORS_PROC_REAL_READ) 
	{
	   lm87_update_client(client);

	   /* find out which temp. is being requested */
	   if (ctl_name == LM87_SYSCTL_TEMP3) 
	   {
		results[0] = TEMP_LIMIT_FROM_REG(data->ext2_temp_max);
		results[1] = TEMP_LIMIT_FROM_REG(data->ext2_temp_min);
		results[2] = TEMP_FROM_REG(data->ext2_temp);
	   }
	   else if(ctl_name == LM87_SYSCTL_TEMP2)
	   {
		results[0] = TEMP_LIMIT_FROM_REG(data->ext_temp_max);
		results[1] = TEMP_LIMIT_FROM_REG(data->ext_temp_min);
		results[2] = TEMP_FROM_REG(data->ext_temp);
	   }
	   else if(ctl_name == LM87_SYSCTL_TEMP1)
	   {
		results[0] = TEMP_LIMIT_FROM_REG(data->int_temp_max);
		results[1] = TEMP_LIMIT_FROM_REG(data->int_temp_min);
		results[2] = TEMP_FROM_REG(data->int_temp);
	   }
	   *nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
	           if (ctl_name == LM87_SYSCTL_TEMP3) {
			data->ext2_temp_max = TEMP_LIMIT_TO_REG(results[0]);
			lm87_write_value(client, LM87_REG_2_5V_EXT_TEMP_2_HIGH,
					    data->ext2_temp_max);
		   }
		   if (ctl_name == LM87_SYSCTL_TEMP2) {
			data->ext_temp_max = TEMP_LIMIT_TO_REG(results[0]);
			lm87_write_value(client, LM87_REG_EXT_TEMP_1_HIGH,
					    data->ext_temp_max);
		   }
		   if (ctl_name == LM87_SYSCTL_TEMP1) {
			data->int_temp_max = TEMP_LIMIT_TO_REG(results[0]);
			lm87_write_value(client, LM87_REG_INT_TEMP_HIGH,
					    data->int_temp_max);
	           }
		}
		if (*nrels_mag >= 2) {
	           if (ctl_name == LM87_SYSCTL_TEMP3) {
			data->ext2_temp_min = TEMP_LIMIT_TO_REG(results[1]);
			lm87_write_value(client, LM87_REG_2_5V_EXT_TEMP_2_LOW,
					    data->ext2_temp_min);
		   }
		   if (ctl_name == LM87_SYSCTL_TEMP2) {
			data->ext_temp_min = TEMP_LIMIT_TO_REG(results[1]);
			lm87_write_value(client, LM87_REG_EXT_TEMP_1_LOW,
					    data->ext_temp_min);
		   }
		   if (ctl_name == LM87_SYSCTL_TEMP1) {
			data->int_temp_min = TEMP_LIMIT_TO_REG(results[1]);
			lm87_write_value(client, LM87_REG_INT_TEMP_LOW,
					    data->int_temp_min);
	           }
		}
	}
}

void lm87_alarms(struct i2c_client *client, int operation, int ctl_name,
		    int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;
	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		results[0] = ALARMS_FROM_REG(data->alarms);
		*nrels_mag = 1;
	}
}

void lm87_fan_div(struct i2c_client *client, int operation,
		     int ctl_name, int *nrels_mag, long *results)
{
/* This gets a little hairy depending on the hardware config */

	struct lm87_data *data = client->data;
	int old;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
#ifndef LM87_AIN1
		results[0] = DIV_FROM_REG(data->fan_div);
# ifndef LM87_AIN2
		results[1] = DIV_FROM_REG(data->fan2_div);
		*nrels_mag = 2;
# else
		*nrels_mag = 1;
# endif
#else /* Must be referring to fan 2 */
		results[0] = DIV_FROM_REG(data->fan2_div);
		*nrels_mag = 1;
#endif
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		old = lm87_read_value(client, LM87_REG_VID_FAN_DIV);
/* Note: it's OK to change fan2 div even if fan2 isn't enabled */
#ifndef LM87_AIN1
		if (*nrels_mag >= 2) {
			data->fan2_div = DIV_TO_REG(results[1]);
			old = (old & 0x3f) | (data->fan2_div << 6);
		}
		if (*nrels_mag >= 1) {
			data->fan_div = DIV_TO_REG(results[0]);
			old = (old & 0xcf) | (data->fan_div << 4);
			lm87_write_value(client, LM87_REG_VID_FAN_DIV, old);
		}
#else /* Must be referring to fan 2 */
		if (*nrels_mag >= 1) {
			data->fan2_div = DIV_TO_REG(results[0]);
			old = (old & 0xcf) | (data->fan2_div << 6);
			lm87_write_value(client, LM87_REG_VID_FAN_DIV, old);
		}
#endif
	}
}

void lm87_analog_out(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		results[0] = data->analog_out;
		*nrels_mag = 1;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->analog_out = results[0];
			lm87_write_value(client, LM87_REG_ANALOG_OUT,
					    data->analog_out);
		}
	}
}

void lm87_vid(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct lm87_data *data = client->data;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 2;
	else if (operation == SENSORS_PROC_REAL_READ) {
		lm87_update_client(client);
		results[0] = VID_FROM_REG(data->vid);
        *nrels_mag = 1;
	}
}

int __init sensors_lm87_init(void)
{
	int res;

	printk("lm87.o version %s (%s)\n", LM_VERSION, LM_DATE);
	lm87_initialized = 0;

	if ((res = i2c_add_driver(&LM87_driver))) {
		printk
		    ("lm87.o: Driver registration failed, module not inserted.\n");
		lm87_cleanup();
		return res;
	}
	lm87_initialized++;
	return 0;
}

int __init lm87_cleanup(void)
{
	int res;

	if (lm87_initialized >= 1) {
		if ((res = i2c_del_driver(&LM87_driver))) {
			printk
			    ("lm87.o: Driver deregistration failed, module not removed.\n");
			return res;
		}
		lm87_initialized--;
	}
	return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_AUTHOR
    ("Frodo Looijaard <frodol@dds.nl>,
      Philip Edelbrock <phil@netroedge.com>, 
      Mark Studebaker <mdsxyz123@yahoo.com>,
      and Stephen Rousset <stephen.rousset@rocketlogix.com>");

MODULE_DESCRIPTION("LM87 driver");

int init_module(void)
{
	return sensors_lm87_init();
}

int cleanup_module(void)
{
	return lm87_cleanup();
}

#endif				/* MODULE */
