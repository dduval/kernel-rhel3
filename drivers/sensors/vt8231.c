/*
    vt8231.c - Part of lm_sensors, Linux kernel modules
                for hardware monitoring
                
    Copyright (c) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>

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

/* Supports VIA VT8231 Super I/O embedded sensors */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/sysctl.h>
#include <linux/pci.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/sensors.h>
#include <linux/sensors.h>
#include "sensors_vid.h"
#include <linux/init.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)) || \
    (LINUX_VERSION_CODE == KERNEL_VERSION(2,3,0))
#define init_MUTEX(s) do { *(s) = MUTEX; } while(0)
#endif

#ifndef I2C_DRIVERID_VT8231
#define I2C_DRIVERID_VT8231 1034
#endif

#ifndef THIS_MODULE
#define THIS_MODULE NULL
#endif

static int force_addr = 0;
MODULE_PARM(force_addr, "i");
MODULE_PARM_DESC(force_addr,
		 "Initialize the base address of the sensors");

static unsigned short normal_i2c[] = { SENSORS_I2C_END };
static unsigned short normal_i2c_range[] = { SENSORS_I2C_END };
static unsigned int normal_isa[] = { 0x0000, SENSORS_ISA_END };
static unsigned int normal_isa_range[] = { SENSORS_ISA_END };

SENSORS_INSMOD_1(vt8231);

#define VIA686A_EXTENT 0x80
#define VIA686A_BASE_REG 0x70
#define VIA686A_ENABLE_REG 0x74

/* pwm numbered 1-2 */
#define VT8231_REG_PWM(nr) (0x5f + (nr))
#define VT8231_REG_PWM_CTL 0x51

/* The VT8231 registers */
/* We define the sensors as follows. Somewhat convoluted to minimize
   changes from via686a.
	Sensor		Voltage Mode	Temp Mode
	--------	------------	---------
	Reading 1			temp3
	Reading 3			temp1	not in vt8231
	UCH1/Reading2	in0		temp2
	UCH2		in1		temp4
	UCH3		in2		temp5
	UCH4		in3		temp6
	UCH5		in4		temp7
	3.3V		in5
	-12V		in6			not in vt8231
*/

/* ins numbered 0-6 */
#define VT8231_REG_IN_MAX(nr) ((nr)==0 ? 0x3d : 0x2b + (((nr)+1) * 2))
#define VT8231_REG_IN_MIN(nr) ((nr)==0 ? 0x3e : 0x2c + (((nr)+1) * 2))
#define VT8231_REG_IN(nr)     (0x21 + (nr))

/* fans numbered 1-2 */
#define VT8231_REG_FAN_MIN(nr) (0x3a + (nr))
#define VT8231_REG_FAN(nr)     (0x28 + (nr))

static const u8 regtemp[] = { 0x20, 0x21, 0x1f, 0x22, 0x23, 0x24, 0x25 };
static const u8 regover[] = { 0x39, 0x3d, 0x1d, 0x2b, 0x2d, 0x2f, 0x31 };
static const u8 reghyst[] = { 0x3a, 0x3e, 0x1e, 0x2c, 0x2e, 0x30, 0x32 };

/* temps numbered 1-7 */
#define VT8231_REG_TEMP(nr)		(regtemp[(nr) - 1])
#define VT8231_REG_TEMP_OVER(nr)	(regover[(nr) - 1])
#define VT8231_REG_TEMP_HYST(nr)	(reghyst[(nr) - 1])
#define VT8231_REG_TEMP_LOW3	0x4b	/* bits 7-6 */
#define VT8231_REG_TEMP_LOW2	0x49	/* bits 5-4 */
#define VT8231_REG_TEMP_LOW47	0x4d

#define VT8231_REG_CONFIG 0x40
#define VT8231_REG_ALARM1 0x41
#define VT8231_REG_ALARM2 0x42
#define VT8231_REG_VID    0x45
#define VT8231_REG_FANDIV 0x47
#define VT8231_REG_UCH_CONFIG 0x4a
#define VT8231_REG_TEMP1_CONFIG 0x4b
#define VT8231_REG_TEMP2_CONFIG 0x4c

/* temps 1-7; voltages 0-6 */
#define ISTEMP(i, ch_config) ((i) == 1 ? 1 : \
			      (i) == 3 ? 1 : \
			      (i) == 2 ? (ch_config) & 0x01 : \
			      ((ch_config) >> ((i)-1)) & 0x01)
#define ISVOLT(i, ch_config) ((i) > 4 ? 1 : !(((ch_config) >> ((i)+2)) & 0x01))

#define DIV_FROM_REG(val) (1 << (val))
#define DIV_TO_REG(val) ((val)==8?3:(val)==4?2:(val)==1?0:1)
#define PWM_FROM_REG(val) (((val) * 1005) / 2550)
#define PWM_TO_REG(val) SENSORS_LIMIT((((val) * 2555) / 1000), 0, 255)

#define TEMP_FROM_REG(val) ((val)*10)
#define TEMP_FROM_REG10(val) (((val)*10)/4)
#define TEMP_TO_REG(val) (SENSORS_LIMIT(((val)<0?(((val)-5)/10):\
                                                 ((val)+5)/10),0,255))
#define IN_FROM_REG(val) /*(((val)*10+5)/10)*/ (val)
#define IN_TO_REG(val)  (SENSORS_LIMIT((((val) * 10 + 5)/10),0,255))


/********* FAN RPM CONVERSIONS ********/
/* But this chip saturates back at 0, not at 255 like all the other chips.
   So, 0 means 0 RPM */
extern inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 0;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1310720 + rpm * div / 2) / (rpm * div), 1, 255);
}

#define MIN_TO_REG(a,b) FAN_TO_REG(a,b)
#define FAN_FROM_REG(val,div) ((val)==0?0:(val)==255?0:1310720/((val)*(div)))

#ifdef MODULE
extern int init_module(void);
extern int cleanup_module(void);
#endif				/* MODULE */

struct vt8231_data {
	struct semaphore lock;
	int sysctl_id;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[7];		/* Register value */
	u8 in_max[7];		/* Register value */
	u8 in_min[7];		/* Register value */
	u16 temp[7];		/* Register value 10 bit */
	u8 temp_over[7];	/* Register value */
	u8 temp_hyst[7];	/* Register value */
	u8 fan[2];		/* Register value */
	u8 fan_min[2];		/* Register value */
	u8 fan_div[2];		/* Register encoding, shifted right */
	u16 alarms;		/* Register encoding */
	u8 pwm[2];		/* Register value */
	u8 pwm_ctl;		/* Register value */
	u8 vid;			/* Register encoding */
	u8 vrm;
	u8 uch_config;
};

#ifdef MODULE
static
#else
extern
#endif
int __init sensors_vt8231_init(void);
static int __init vt8231_cleanup(void);

static int vt8231_attach_adapter(struct i2c_adapter *adapter);
static int vt8231_detect(struct i2c_adapter *adapter, int address,
			  unsigned short flags, int kind);
static int vt8231_detach_client(struct i2c_client *client);
static int vt8231_command(struct i2c_client *client, unsigned int cmd,
			   void *arg);
static void vt8231_inc_use(struct i2c_client *client);
static void vt8231_dec_use(struct i2c_client *client);

static inline int vt_rdval(struct i2c_client *client, u8 register);
static inline void vt8231_write_value(struct i2c_client *client, u8 register,
			       u8 value);
static void vt8231_update_client(struct i2c_client *client);
static void vt8231_init_client(struct i2c_client *client);
static int vt8231_find(int *address);


static void vt8231_fan(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_alarms(struct i2c_client *client, int operation,
			   int ctl_name, int *nrels_mag, long *results);
static void vt8231_fan_div(struct i2c_client *client, int operation,
			    int ctl_name, int *nrels_mag, long *results);
static void vt8231_in(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_pwm(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_vid(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_vrm(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_uch(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);
static void vt8231_temp(struct i2c_client *client, int operation,
			int ctl_name, int *nrels_mag, long *results);

static int vt8231_id = 0;

static struct i2c_driver vt8231_driver = {
	/* name */ "VT8231 sensors driver",
	/* id */ I2C_DRIVERID_VT8231,
	/* flags */ I2C_DF_NOTIFY,
	/* attach_adapter */ &vt8231_attach_adapter,
	/* detach_client */ &vt8231_detach_client,
	/* command */ &vt8231_command,
	/* inc_use */ &vt8231_inc_use,
	/* dec_use */ &vt8231_dec_use
};

static int __initdata vt8231_initialized = 0;

static ctl_table vt8231_dir_table_template[] = {
	{VT8231_SYSCTL_IN0, "in0", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN1, "in1", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN2, "in2", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN3, "in3", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN4, "in4", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN5, "in5", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_IN6, "in6", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_in},
	{VT8231_SYSCTL_TEMP, "temp1", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP2, "temp2", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP3, "temp3", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP4, "temp4", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP5, "temp5", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP6, "temp6", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_TEMP7, "temp7", NULL, 0, 0644, NULL,
	 &i2c_proc_real, &i2c_sysctl_real, NULL, &vt8231_temp},
	{VT8231_SYSCTL_FAN1, "fan1", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_fan},
	{VT8231_SYSCTL_FAN2, "fan2", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_fan},
	{VT8231_SYSCTL_FAN_DIV, "fan_div", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_fan_div},
	{VT8231_SYSCTL_ALARMS, "alarms", NULL, 0, 0444, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_alarms},
	{VT8231_SYSCTL_PWM1, "pwm1", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_pwm},
	{VT8231_SYSCTL_PWM2, "pwm2", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_pwm},
	{VT8231_SYSCTL_VID, "vid", NULL, 0, 0444, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_vid},
	{VT8231_SYSCTL_VRM, "vrm", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_vrm},
	{VT8231_SYSCTL_UCH, "uch_config", NULL, 0, 0644, NULL, &i2c_proc_real,
	 &i2c_sysctl_real, NULL, &vt8231_uch},
	{0}
};

static struct pci_dev *s_bridge;

int vt8231_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_detect(adapter, &addr_data, vt8231_detect);
}

/* Locate chip and get correct base address */
int vt8231_find(int *address)
{
	u16 val;

	if (!pci_present())
		return -ENODEV;

	if (!(s_bridge = pci_find_device(PCI_VENDOR_ID_VIA,
					 0x8235, NULL)))
		return -ENODEV;

	if (PCIBIOS_SUCCESSFUL !=
	    pci_read_config_word(s_bridge, VIA686A_BASE_REG, &val))
		return -ENODEV;
	*address = val & ~(VIA686A_EXTENT - 1);
	if (*address == 0 && force_addr == 0) {
		printk("vt8231.o: base address not set - upgrade BIOS or use force_addr=0xaddr\n");
		return -ENODEV;
	}
	if (force_addr)
		*address = force_addr;	/* so detect will get called */

	return 0;
}

int vt8231_detect(struct i2c_adapter *adapter, int address,
		   unsigned short flags, int kind)
{
	int i;
	struct i2c_client *new_client;
	struct vt8231_data *data;
	int err = 0;
	const char *type_name = "vt8231";
	u16 val;

	if (!i2c_is_isa_adapter(adapter)) {
		return 0;
	}

	/* 8231 requires multiple of 256 */
	if(force_addr)
		address = force_addr & 0xFF00;
	if (check_region(address, VIA686A_EXTENT)) {
		printk("vt8231.o: region 0x%x already in use!\n",
		       address);
		return -ENODEV;
	}

	if(force_addr) {
		printk("vt8231.o: forcing ISA address 0x%04X\n", address);
		if (PCIBIOS_SUCCESSFUL !=
		    pci_write_config_word(s_bridge, VIA686A_BASE_REG, address))
			return -ENODEV;
	}
	if (PCIBIOS_SUCCESSFUL !=
	    pci_read_config_word(s_bridge, VIA686A_ENABLE_REG, &val))
		return -ENODEV;
	if (!(val & 0x0001)) {
		printk("vt8231.o: enabling sensors\n");
		if (PCIBIOS_SUCCESSFUL !=
		    pci_write_config_word(s_bridge, VIA686A_ENABLE_REG,
		                      val | 0x0001))
			return -ENODEV;
	}

	if (!(new_client = kmalloc(sizeof(struct i2c_client) +
				   sizeof(struct vt8231_data),
				   GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}

	data = (struct vt8231_data *) (new_client + 1);
	new_client->addr = address;
	init_MUTEX(&data->lock);
	new_client->data = data;
	new_client->adapter = adapter;
	new_client->driver = &vt8231_driver;
	new_client->flags = 0;

	/* Reserve the ISA region */
	request_region(address, VIA686A_EXTENT, "vt8231-sensors");

	/* Fill in the remaining client fields and put into the global list */
	strcpy(new_client->name, "Via 8231 Integrated Sensors");

	new_client->id = vt8231_id++;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR3;

	/* Register a new directory entry with module sensors */
	if ((i = i2c_register_entry((struct i2c_client *) new_client,
					type_name,
					vt8231_dir_table_template,
					THIS_MODULE)) < 0) {
		err = i;
		goto ERROR4;
	}
	data->sysctl_id = i;

	vt8231_init_client(new_client);
	return 0;

      ERROR4:
	i2c_detach_client(new_client);
      ERROR3:
	release_region(address, VIA686A_EXTENT);
	kfree(new_client);
      ERROR0:
	return err;
}

int vt8231_detach_client(struct i2c_client *client)
{
	int err;

	i2c_deregister_entry(((struct vt8231_data *) (client->data))->
				 sysctl_id);

	if ((err = i2c_detach_client(client))) {
		printk
		    ("vt8231.o: Client deregistration failed, client not detached.\n");
		return err;
	}

	release_region(client->addr, VIA686A_EXTENT);
	kfree(client);

	return 0;
}

int vt8231_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	return 0;
}

void vt8231_inc_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void vt8231_dec_use(struct i2c_client *client)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static inline int vt_rdval(struct i2c_client *client, u8 reg)
{
	return (inb_p(client->addr + reg));
}

static inline void vt8231_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	outb_p(value, client->addr + reg);
}

void vt8231_init_client(struct i2c_client *client)
{
	struct vt8231_data *data = client->data;

	data->vrm = DEFAULT_VRM;
	/* set "default" interrupt mode for alarms, which isn't the default */
	vt8231_write_value(client, VT8231_REG_TEMP1_CONFIG, 0);
	vt8231_write_value(client, VT8231_REG_TEMP2_CONFIG, 0);
}

void vt8231_update_client(struct i2c_client *client)
{
	struct vt8231_data *data = client->data;
	int i, j;

	down(&data->update_lock);

	if ((jiffies - data->last_updated > HZ + HZ / 2) ||
	    (jiffies < data->last_updated) || !data->valid) {
		data->uch_config = vt_rdval(client, VT8231_REG_UCH_CONFIG);
		for (i = 0; i <= 6; i++) {
			if(ISVOLT(i, data->uch_config)) {
				data->in[i] = vt_rdval(client, VT8231_REG_IN(i));
				data->in_min[i] = vt_rdval(client,
				                        VT8231_REG_IN_MIN(i));
				data->in_max[i] = vt_rdval(client,
				                        VT8231_REG_IN_MAX(i));
			} else {
				data->in[i] = 0;
				data->in_min[i] = 0;
				data->in_max[i] = 0;
			}
		}
		for (i = 1; i <= 2; i++) {
			data->fan[i - 1] = vt_rdval(client, VT8231_REG_FAN(i));
			data->fan_min[i - 1] = vt_rdval(client,
						     VT8231_REG_FAN_MIN(i));
		}
		for (i = 1; i <= 7; i++) {
			if(ISTEMP(i, data->uch_config)) {
				data->temp[i - 1] = vt_rdval(client,
					             VT8231_REG_TEMP(i)) << 2;
				switch(i) {
					case 1:
						/* ? */
						j = 0;
						break;
					case 2:
						j = (vt_rdval(client,
						  VT8231_REG_TEMP_LOW2) &
						                    0x30) >> 4;
						break;
					case 3:
						j = (vt_rdval(client,
						  VT8231_REG_TEMP_LOW3) &
						                    0xc0) >> 6;
						break;
					case 4:
					case 5:
					case 6:
					case 7:
					default:
						j = (vt_rdval(client,
						  VT8231_REG_TEMP_LOW47) >>
						            ((i-4)*2)) & 0x03;	
						break;
	
				}
				data->temp[i - 1] |= j;
				data->temp_over[i - 1] = vt_rdval(client,
					              VT8231_REG_TEMP_OVER(i));
				data->temp_hyst[i - 1] = vt_rdval(client,
					              VT8231_REG_TEMP_HYST(i));
			} else {
				data->temp[i - 1] = 0;
				data->temp_over[i - 1] = 0;
				data->temp_hyst[i - 1] = 0;
			}
		}

		for (i = 1; i <= 2; i++) {
			data->fan[i - 1] = vt_rdval(client, VT8231_REG_FAN(i));
			data->fan_min[i - 1] = vt_rdval(client,
			                                VT8231_REG_FAN_MIN(i));
			data->pwm[i - 1] = vt_rdval(client, VT8231_REG_PWM(i));
		}

		data->pwm_ctl = vt_rdval(client, VT8231_REG_PWM_CTL);
		i = vt_rdval(client, VT8231_REG_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = i >> 6;
		data->alarms = vt_rdval(client, VT8231_REG_ALARM1) |
		                    (vt_rdval(client, VT8231_REG_ALARM2) << 8);
		data->vid= vt_rdval(client, VT8231_REG_VID) & 0x1f;
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);
}


void vt8231_in(struct i2c_client *client, int operation, int ctl_name,
		int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	int nr = ctl_name - VT8231_SYSCTL_IN0;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 2;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = IN_FROM_REG(data->in_min[nr]);
		results[1] = IN_FROM_REG(data->in_max[nr]);
		results[2] = IN_FROM_REG(data->in[nr]);
		*nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->in_min[nr] = IN_TO_REG(results[0]);
			vt8231_write_value(client, VT8231_REG_IN_MIN(nr),
					    data->in_min[nr]);
		}
		if (*nrels_mag >= 2) {
			data->in_max[nr] = IN_TO_REG(results[1]);
			vt8231_write_value(client, VT8231_REG_IN_MAX(nr),
					    data->in_max[nr]);
		}
	}
}

void vt8231_fan(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	int nr = ctl_name - VT8231_SYSCTL_FAN1 + 1;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = FAN_FROM_REG(data->fan_min[nr - 1],
					  DIV_FROM_REG(data->fan_div
						       [nr - 1]));
		results[1] = FAN_FROM_REG(data->fan[nr - 1],
				 DIV_FROM_REG(data->fan_div[nr - 1]));
		*nrels_mag = 2;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->fan_min[nr - 1] = MIN_TO_REG(results[0],
							   DIV_FROM_REG
							   (data->
							    fan_div[nr-1]));
			vt8231_write_value(client, VT8231_REG_FAN_MIN(nr),
					    data->fan_min[nr - 1]);
		}
	}
}


void vt8231_temp(struct i2c_client *client, int operation, int ctl_name,
		  int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	int nr = ctl_name - VT8231_SYSCTL_TEMP;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 1;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = TEMP_FROM_REG(data->temp_over[nr]);
		results[1] = TEMP_FROM_REG(data->temp_hyst[nr]);
		results[2] = TEMP_FROM_REG10(data->temp[nr]);
		*nrels_mag = 3;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->temp_over[nr] = TEMP_TO_REG(results[0]);
			vt8231_write_value(client,
					    VT8231_REG_TEMP_OVER(nr + 1),
					    data->temp_over[nr]);
		}
		if (*nrels_mag >= 2) {
			data->temp_hyst[nr] = TEMP_TO_REG(results[1]);
			vt8231_write_value(client,
					    VT8231_REG_TEMP_HYST(nr + 1),
					    data->temp_hyst[nr]);
		}
	}
}

void vt8231_alarms(struct i2c_client *client, int operation, int ctl_name,
		    int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = data->alarms;
		*nrels_mag = 1;
	}
}

void vt8231_fan_div(struct i2c_client *client, int operation,
		     int ctl_name, int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	int old;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = DIV_FROM_REG(data->fan_div[0]);
		results[1] = DIV_FROM_REG(data->fan_div[1]);
		*nrels_mag = 2;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		old = vt_rdval(client, VT8231_REG_FANDIV);
		if (*nrels_mag >= 2) {
			data->fan_div[1] = DIV_TO_REG(results[1]);
			old = (old & 0x3f) | (data->fan_div[1] << 6);
		}
		if (*nrels_mag >= 1) {
			data->fan_div[0] = DIV_TO_REG(results[0]);
			old = (old & 0xcf) | (data->fan_div[0] << 4);
			vt8231_write_value(client, VT8231_REG_FANDIV, old);
		}
	}
}

void vt8231_pwm(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	int nr = 1 + ctl_name - VT8231_SYSCTL_PWM1;

	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = PWM_FROM_REG(data->pwm[nr - 1]);
		results[1] = (data->pwm_ctl >> (3 + (4 * (nr - 1)))) & 1;
		*nrels_mag = 2;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->pwm[nr - 1] = PWM_TO_REG(results[0]);
			if (*nrels_mag >= 2) {
				if(results[1]) {
					data->pwm_ctl |=
					          (0x08 << (4 * (nr - 1)));
					vt8231_write_value(client,
					                   VT8231_REG_PWM_CTL, 
				                           data->pwm_ctl);
				} else {
					data->pwm_ctl &=
					        ~ (0x08 << (4 * (nr - 1)));
					vt8231_write_value(client,
					                   VT8231_REG_PWM_CTL, 
				                           data->pwm_ctl);
				}
			}
			vt8231_write_value(client, VT8231_REG_PWM(nr),
					    data->pwm[nr - 1]);
		}
	}
}

void vt8231_vid(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 3;
	else if (operation == SENSORS_PROC_REAL_READ) {
		vt8231_update_client(client);
		results[0] = vid_from_reg(data->vid, data->vrm);
		*nrels_mag = 1;
	}
}

void vt8231_vrm(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 1;
	else if (operation == SENSORS_PROC_REAL_READ) {
		results[0] = data->vrm;
		*nrels_mag = 1;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1)
			data->vrm = results[0];
	}
}

void vt8231_uch(struct i2c_client *client, int operation, int ctl_name,
		 int *nrels_mag, long *results)
{
	struct vt8231_data *data = client->data;
	if (operation == SENSORS_PROC_REAL_INFO)
		*nrels_mag = 0;
	else if (operation == SENSORS_PROC_REAL_READ) {
		results[0] = data->uch_config;
		*nrels_mag = 1;
	} else if (operation == SENSORS_PROC_REAL_WRITE) {
		if (*nrels_mag >= 1) {
			data->uch_config = results[0] & 0x7c;
			vt8231_write_value(client, VT8231_REG_UCH_CONFIG,
			                   results[0] & 0x7c);
		}
	}
}

int __init sensors_vt8231_init(void)
{
	int res, addr;

	printk("vt8231.o version %s (%s)\n", LM_VERSION, LM_DATE);
	vt8231_initialized = 0;

	if (vt8231_find(&addr)) {
		printk("vt8231.o: VT8231 not detected, module not inserted.\n");
		return -ENODEV;
	}
	normal_isa[0] = addr;

	if ((res = i2c_add_driver(&vt8231_driver))) {
		printk
		    ("vt8231.o: Driver registration failed, module not inserted.\n");
		vt8231_cleanup();
		return res;
	}
	vt8231_initialized++;
	return 0;
}

int __init vt8231_cleanup(void)
{
	int res;

	if (vt8231_initialized >= 1) {
		if ((res = i2c_del_driver(&vt8231_driver))) {
			printk
			    ("vt8231.o: Driver deregistration failed, module not removed.\n");
			return res;
		}
		vt8231_initialized--;
	}
	return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE

MODULE_AUTHOR("Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("VT8231 sensors");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

int init_module(void)
{
	return sensors_vt8231_init();
}

int cleanup_module(void)
{
	return vt8231_cleanup();
}

#endif				/* MODULE */
