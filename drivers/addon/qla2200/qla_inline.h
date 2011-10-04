/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/*
 * This file includes a set of defines that are required to compile the 
 * source code for qla2xxx module
 */

static inline int 
check_device_id(scsi_qla_host_t *ha)
{
	return (ha->device_id == QLA6322_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID); 
}

static inline int 
check_all_device_ids(scsi_qla_host_t *ha)
{
	return (ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID ||
	    ha->device_id == QLA2312_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID);
}

#define qla2x00_is_guadalupe(ha) ((ha->device_id == QLA2322_DEVICE_ID) && \
                              (ha->subsystem_device == 0x0170) && \
                              (ha->subsystem_vendor == 0x1028))

static inline int 
check_24xx_or_54xx_device_ids(scsi_qla_host_t *ha)
{
	return (((ha->device_id & 0xff00) == 0x2400) || 
		((ha->device_id & 0xff00) == 0x5400));
}

static inline int 
check_25xx_device_ids(scsi_qla_host_t *ha)
{
	return ((ha->device_id & 0xff00) == 0x2500);
}
static inline void 
set_model_number(scsi_qla_host_t *ha)
{
 	if (ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID)
		sprintf(ha->model_number, "QLA63xx");
 	else
		sprintf(ha->model_number, "QLA23xx");
}

static inline uint8_t *host_to_fcp_swap(uint8_t *, uint32_t);

/**
 * host_to_fcp_swap() - 
 * @fcp: 
 * @bsize: 
 *
 * Returns 
 */
static inline uint8_t *
host_to_fcp_swap(uint8_t *fcp, uint32_t bsize)
{
	uint32_t *ifcp = (uint32_t *) fcp;
	uint32_t *ofcp = (uint32_t *) fcp;
	uint32_t iter = bsize >> 2;

	for (; iter ; iter--)
		*ofcp++ = swab32(*ifcp++);

	return (fcp);
}

