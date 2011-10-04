/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISPFBLITE device driver for Linux 2.4.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

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

static inline void 
set_model_number(scsi_qla_host_t *ha)
{
 	if (ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID)
		sprintf(ha->model_number, "QLA63xx");
 	else
		sprintf(ha->model_number, "QLA23xx");
}
