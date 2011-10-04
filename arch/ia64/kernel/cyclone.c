#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/param.h>
#include <asm/io.h>

/* IBM Summit (EXA) Cyclone Timer code*/
#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ 100000000

int use_cyclone;
int __init cyclone_setup(char *str) 
{
		 use_cyclone = 1;
		 return 1;
}

static u32* volatile cyclone_timer;		 /* Cyclone MPMC0 register */
static u32 last_tick_cyclone;

static unsigned long delay_at_last_interrupt;

unsigned long do_gettimeoffset_cyclone(void)
{
		 u32 offset;

		 /* Read the cyclone timer */
		 offset = cyclone_timer[0];
		 /* .. relative to previous jiffy */
		 offset = offset - last_tick_cyclone;

		 /* convert cyclone ticks to microseconds */		 
		 offset = offset/(CYCLONE_TIMER_FREQ/1000000);

		 /* our adjusted time offset in microseconds */
		 return delay_at_last_interrupt + (unsigned long)offset;
}

void mark_timeoffset_cyclone(void)
{
		 last_tick_cyclone += CYCLONE_TIMER_FREQ/HZ;
}


void __init init_cyclone_clock(void)
{
		 u64* reg;		 
		 u64 base;		 		 /* saved cyclone base address */
		 u64 offset;		 		 /* offset from pageaddr to cyclone_timer register */
		 int i;
		 
		 printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

		 /* find base address */
		 offset = (CYCLONE_CBAR_ADDR);
		 reg = (u64*)ioremap_nocache(offset, sizeof(u64));
		 if(!reg){
		 		 printk(KERN_ERR "Summit chipset: Could not find valid CBAR register.\n");
		 		 use_cyclone = 0;
		 		 return;
		 }
		 base = *reg;		 
		 if(!base){
		 		 printk(KERN_ERR "Summit chipset: Could not find valid CBAR value.\n");
		 		 use_cyclone = 0;
		 		 return;
		 }
		 iounmap(reg);
		 		 
		 /* setup PMCC */
		 offset = (base + CYCLONE_PMCC_OFFSET);
		 reg = (u64*)ioremap_nocache(offset, sizeof(u64));
		 if(!reg){
		 		 printk(KERN_ERR "Summit chipset: Could not find valid PMCC register.\n");
		 		 use_cyclone = 0;
		 		 return;
		 }
		 reg[0] = 0x00000001;
		 iounmap(reg);
		 
		 /* setup MPCS */
		 offset = (base + CYCLONE_MPCS_OFFSET);
		 reg = (u64*)ioremap_nocache(offset, sizeof(u64));
		 if(!reg){
		 		 printk(KERN_ERR "Summit chipset: Could not find valid MPCS register.\n");
		 		 use_cyclone = 0;
		 		 return;
		 }
		 reg[0] = 0x00000001;
		 iounmap(reg);
		 
		 /* map in cyclone_timer */
		 offset = (base + CYCLONE_MPMC_OFFSET);
		 cyclone_timer = (u32*)ioremap_nocache(offset, sizeof(u32));
		 if(!cyclone_timer){
		 		 printk(KERN_ERR "Summit chipset: Could not find valid MPMC register.\n");
		 		 use_cyclone = 0;
		 		 return;
		 }

		 /*quick test to make sure its ticking*/
		 for(i=0; i<3; i++){
		 		 u32 old = cyclone_timer[0];
		 		 int stall = 100;
		 		 while(stall--) barrier();
		 		 if(cyclone_timer[0] == old){
		 		 		 printk(KERN_ERR "Summit chipset: Counter not counting! DISABLED\n");
		 		 		 iounmap(cyclone_timer);
		 		 		 cyclone_timer = 0;
		 		 		 use_cyclone = 0;
		 		 		 return;
		 		 }
		 }
		 last_tick_cyclone = cyclone_timer[0];
}

