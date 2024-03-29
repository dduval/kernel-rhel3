/*
 *  include/asm-s390x/dma.h
 *
 *  S390 version
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

#include <asm/io.h>		/* need byte IO */

/* The I/O subsystem can access only memory below 2GB.
   We use the existing DMA zone mechanism to handle this. */
#define MAX_DMA_ADDRESS         0x80000000

/* This obviously does nothing good on our platform, it lets scsi to link */
extern void free_dma(unsigned int dmanr);

#endif /* _ASM_DMA_H */
