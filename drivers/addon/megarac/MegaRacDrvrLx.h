/*****************************************************************************
 *
 *  MegaRacDrvrLx.h : MegaRac device driver definitions for Linux
 *
 ****************************************************************************/
#ifndef MEGARAC_DRIVER_LX_H
#define MEGARAC_DRIVER_LX_H

/* ioctl stuff */

#define RAC_IOC_MAGIC       0x0e /* part of vendor ID */
#define IOCTL_ISSUE_RCS     0x01
#define IOCTL_GET_GRAPHICS  0x02
#define IOCTL_EVENT_WAIT    0x03
#define IOCTL_API_INTERNAL  0x07
#define IOCTL_RESET_CARD    0x09

#define MEGARAC_DEVICE_NAME "/dev/rac0"

typedef struct _MEGARAC_IO_BUFS {
    unsigned long  ioControlCode;
    void          *requestBuf;
    unsigned long  requestBufLen;
    void          *responseBuf;
    unsigned long  responseBufLen;
} MEGARAC_IO_BUFS;

#endif /* MEGARAC_DRIVER_LX_H */
