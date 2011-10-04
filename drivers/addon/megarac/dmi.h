/*
 ****************************************************************************
 *
 *          File Name   :       DMI.H
 *
 *          Author      :       Mike Bartholomew.
 *
 *          Date        :       08 Oct 1998
 *
 *          Purpose     :       This include file used for definitions 
 *                              associated with detecting DMI information
 *                              within the host system BIOS
 *
 *          Copyright   :       American Megatrends Inc., (C) 1997-1998
 *                              All rights reserved.
 *
 ****************************************************************************
 */
#ifndef __AMI_DMI_H__
#define __AMI_DMI_H__

#define MAX_DMI_BUFFER 8192
#define BIOS_START_ADDR 0x000f0000l
#define BIOS_STOP_ADDR 0x00100000l

typedef struct _dmi_header{
   unsigned char  Signature[5];
   unsigned char  Checksum;
   unsigned short StructLength;
   unsigned char  StructAddr[4];
   unsigned short NumberOfStructs;
   unsigned char  BCDRevision;
   unsigned char  Reserved;
}DMI_HEADER;

#endif
/***************************** End of DMI.H *******************************/

