#ifndef __LIBATA_COMPAT_H__
#define __LIBATA_COMPAT_H__

/* For 2.6.x compatibility */
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#define SAM_STAT_GOOD            0x00
#define SAM_STAT_CHECK_CONDITION 0x02
#define SAM_STAT_CONDITION_MET   0x04
#define SAM_STAT_BUSY            0x08
#define SAM_STAT_INTERMEDIATE    0x10
#define SAM_STAT_INTERMEDIATE_CONDITION_MET 0x14
#define SAM_STAT_RESERVATION_CONFLICT 0x18
#define SAM_STAT_COMMAND_TERMINATED 0x22        /* obsolete in SAM-3 */
#define SAM_STAT_TASK_SET_FULL   0x28
#define SAM_STAT_ACA_ACTIVE      0x30
#define SAM_STAT_TASK_ABORTED    0x40
#define REPORT_LUNS           0xa0
#define READ_16               0x88
#define WRITE_16              0x8a
#define SERVICE_ACTION_IN     0x9e
#define SAI_READ_CAPACITY_16  0x10

#endif /* __LIBATA_COMPAT_H__ */

