#ifndef _ASM_IA64_DMA_H
#define _ASM_IA64_DMA_H

/*
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <asm/io.h>		/* need byte IO */
#include <linux/delay.h>

extern unsigned long MAX_DMA_ADDRESS;

#define MAX_DMA_CHANNELS 8

#ifdef CONFIG_PCI
  extern int isa_dma_bridge_buggy;
#else
# define isa_dma_bridge_buggy 	(0)
#endif

#ifdef HAVE_REALLY_SLOW_DMA_CONTROLLER
#define dma_outb	outb_p
#else
#define dma_outb	outb
#endif

#define dma_inb		inb

#define MAX_DMA_CHANNELS	8

/* DMA controllers */
#define IO_DMA1_BASE	0x00	/* 8 bit slave DMA, channels 0..3 */
#define IO_DMA2_BASE	0xC0	/* 16 bit master DMA, ch 4(=slave input)..7 */

/* DMA controller registers */
#define DMA1_CMD_REG		0x08	/* command register (w) */
#define DMA1_STAT_REG		0x08	/* status register (r) */
#define DMA1_REQ_REG            0x09    /* request register (w) */
#define DMA1_MASK_REG		0x0A	/* single-channel mask (w) */
#define DMA1_MODE_REG		0x0B	/* mode register (w) */
#define DMA1_CLEAR_FF_REG	0x0C	/* clear pointer flip-flop (w) */
#define DMA1_TEMP_REG           0x0D    /* Temporary Register (r) */
#define DMA1_RESET_REG		0x0D	/* Master Clear (w) */
#define DMA1_CLR_MASK_REG       0x0E    /* Clear Mask */
#define DMA1_MASK_ALL_REG       0x0F    /* all-channels mask (w) */

#define DMA2_CMD_REG		0xD0	/* command register (w) */
#define DMA2_STAT_REG		0xD0	/* status register (r) */
#define DMA2_REQ_REG            0xD2    /* request register (w) */
#define DMA2_MASK_REG		0xD4	/* single-channel mask (w) */
#define DMA2_MODE_REG		0xD6	/* mode register (w) */
#define DMA2_CLEAR_FF_REG	0xD8	/* clear pointer flip-flop (w) */
#define DMA2_TEMP_REG           0xDA    /* Temporary Register (r) */
#define DMA2_RESET_REG		0xDA	/* Master Clear (w) */
#define DMA2_CLR_MASK_REG       0xDC    /* Clear Mask */
#define DMA2_MASK_ALL_REG       0xDE    /* all-channels mask (w) */

#define DMA_ADDR_0              0x00    /* DMA address registers */
#define DMA_ADDR_1              0x02
#define DMA_ADDR_2              0x04
#define DMA_ADDR_3              0x06
#define DMA_ADDR_4              0xC0
#define DMA_ADDR_5              0xC4
#define DMA_ADDR_6              0xC8
#define DMA_ADDR_7              0xCC

#define DMA_CNT_0               0x01    /* DMA count registers */
#define DMA_CNT_1               0x03
#define DMA_CNT_2               0x05
#define DMA_CNT_3               0x07
#define DMA_CNT_4               0xC2
#define DMA_CNT_5               0xC6
#define DMA_CNT_6               0xCA
#define DMA_CNT_7               0xCE

#define DMA_PAGE_0              0x87    /* DMA page registers */
#define DMA_PAGE_1              0x83
#define DMA_PAGE_2              0x81
#define DMA_PAGE_3              0x82
#define DMA_PAGE_5              0x8B
#define DMA_PAGE_6              0x89
#define DMA_PAGE_7              0x8A

#define DMA_MODE_READ	0x44	/* I/O to memory, no autoinit, increment, single mode */
#define DMA_MODE_WRITE	0x48	/* memory to I/O, no autoinit, increment, single mode */
#define DMA_MODE_CASCADE 0xC0   /* pass thru DREQ->HRQ, DACK<-HLDA only */

#define DMA_AUTOINIT	0x10

extern spinlock_t  dma_spin_lock;

static __inline__ unsigned long claim_dma_lock(void)
{
        unsigned long flags;
        spin_lock_irqsave(&dma_spin_lock, flags);
        return flags;
}

static __inline__ void release_dma_lock(unsigned long flags)
{
        spin_unlock_irqrestore(&dma_spin_lock, flags);
}
static __inline__ void enable_dma(unsigned int dmanr)
{
        unsigned char ucDmaCmd=0x00;

        if (dmanr != 4)
        {
                dma_outb(0, DMA2_MASK_REG);  /* This may not be enabled */
                dma_outb(ucDmaCmd, DMA2_CMD_REG);  /* Enable group */
        }
        if (dmanr<=3)
        {
                dma_outb(dmanr,  DMA1_MASK_REG);
                dma_outb(ucDmaCmd, DMA1_CMD_REG);  /* Enable group */
        } else
        {
                dma_outb(dmanr & 3,  DMA2_MASK_REG);
        }
}

static __inline__ void disable_dma(unsigned int dmanr)
{
        if (dmanr<=3)
                dma_outb(dmanr | 4,  DMA1_MASK_REG);
        else
                dma_outb((dmanr & 3) | 4,  DMA2_MASK_REG);
}
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
        if (dmanr<=3)
                dma_outb(0,  DMA1_CLEAR_FF_REG);
        else
                dma_outb(0,  DMA2_CLEAR_FF_REG);
}

static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{
        if (dmanr<=3)
                dma_outb(mode | dmanr,  DMA1_MODE_REG);
        else
                dma_outb(mode | (dmanr&3),  DMA2_MODE_REG);
}

static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int phys)
{
        if (dmanr <= 3)  {
            dma_outb( phys & 0xff, ((dmanr&3)<<1) + IO_DMA1_BASE );
            dma_outb( (phys>>8) & 0xff, ((dmanr&3)<<1) + IO_DMA1_BASE );
        }  else  {
            dma_outb( (phys>>1) & 0xff, ((dmanr&3)<<2) + IO_DMA2_BASE );
            dma_outb( (phys>>9) & 0xff, ((dmanr&3)<<2) + IO_DMA2_BASE );
        }
}

static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
        count--;
        if (dmanr <= 3)  {
            dma_outb( count & 0xff, ((dmanr&3)<<1) + 1 + IO_DMA1_BASE );
            dma_outb( (count>>8) & 0xff, ((dmanr&3)<<1) + 1 + IO_DMA1_BASE );
        } else {
            dma_outb( (count>>1) & 0xff, ((dmanr&3)<<2) + 2 + IO_DMA2_BASE );
            dma_outb( (count>>9) & 0xff, ((dmanr&3)<<2) + 2 + IO_DMA2_BASE );
        }
}

static __inline__ int get_dma_residue(unsigned int dmanr)
{
        unsigned int io_port = (dmanr<=3)? ((dmanr&3)<<1) + 1 + IO_DMA1_BASE
                                         : ((dmanr&3)<<2) + 2 + IO_DMA2_BASE;

        /* using short to get 16-bit wrap around */
        unsigned short count;

        count = 1 + dma_inb(io_port);
        count += dma_inb(io_port) << 8;

        return (dmanr <= 3)? count : (count<<1);
}


/* These are in kernel/dma.c: */
extern int request_dma(unsigned int dmanr, const char * device_id);	/* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	/* release it again */

/* From PCI */

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* _ASM_IA64_DMA_H */
