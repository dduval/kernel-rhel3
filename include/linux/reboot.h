#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H

/*
 * Magic values required to use _reboot() system call.
 */

#define	LINUX_REBOOT_MAGIC1	0xfee1dead
#define	LINUX_REBOOT_MAGIC2	672274793
#define	LINUX_REBOOT_MAGIC2A	85072278
#define	LINUX_REBOOT_MAGIC2B	369367448


/*
 * Commands accepted by the _reboot() system call.
 *
 * RESTART     Restart system using default command and mode.
 * HALT        Stop OS and give system control to ROM monitor, if any.
 * CAD_ON      Ctrl-Alt-Del sequence causes RESTART command.
 * CAD_OFF     Ctrl-Alt-Del sequence sends SIGINT to init task.
 * POWER_OFF   Stop OS and remove all power from system, if possible.
 * RESTART2    Restart system using given command string.
 */

#define	LINUX_REBOOT_CMD_RESTART	0x01234567
#define	LINUX_REBOOT_CMD_HALT		0xCDEF0123
#define	LINUX_REBOOT_CMD_CAD_ON		0x89ABCDEF
#define	LINUX_REBOOT_CMD_CAD_OFF	0x00000000
#define	LINUX_REBOOT_CMD_POWER_OFF	0x4321FEDC
#define	LINUX_REBOOT_CMD_RESTART2	0xA1B2C3D4


#ifdef __KERNEL__

#include <linux/notifier.h>

extern int register_reboot_notifier(struct notifier_block *);
extern int unregister_reboot_notifier(struct notifier_block *);


/*
 * Architecture-specific implementations of sys_reboot commands.
 */

extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);
#ifdef CONFIG_X86_64
extern void (*machine_reset)(void);
#endif

#endif

#endif /* _LINUX_REBOOT_H */
