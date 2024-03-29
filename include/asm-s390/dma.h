/*
 *  include/asm-s390/dma.h
 *
 *  S390 version
 *
 *  This file exists so that an #include <dma.h> doesn't break anything.
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

#include <asm/io.h>		/* need byte IO */

#define MAX_DMA_ADDRESS         0x80000000

/* This obviously does nothing good on our platform, it lets scsi to link */
extern void free_dma(unsigned int dmanr);

#endif /* _ASM_DMA_H */
