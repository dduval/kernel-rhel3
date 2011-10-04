/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/******************************************************************************
* Failover include file
******************************************************************************/
#include "qla2x00.h"
#include "qla_gbl.h"

#include "exioct.h"
#include "qlfo.h"
#include "qla_fo.h"
#include "qlfolimits.h"
#include "qla_cfg.h"


/* This type is used to create a temporary list of port names */
typedef struct _portname_list {
	struct _portname_list *pnext;
	uint8_t 	portname[8];
} portname_list;

/*
 * Global variables
 */
SysFoParams_t qla_fo_params;

/*
 * Local routines
 */
#if !defined(linux)
static int qla2x00_sdm_setup(EXT_IOCTL *cmd_stp, void *arg, int mode);
#endif
static uint32_t qla2x00_fo_get_params(PFO_PARAMS pp);
static uint32_t qla2x00_fo_set_params(PFO_PARAMS pp);
static BOOL qla2x00_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp);
static int qla2x00_fo_get_lun_data(EXT_IOCTL *pext,
    FO_LUN_DATA_INPUT *bp, int mode);
static int qla2x00_fo_set_lun_data(EXT_IOCTL *pext,
    FO_LUN_DATA_INPUT *bp, int mode);
static uint32_t qla2x00_fo_stats(FO_HBA_STAT *stat_p, BOOL reset);

static int qla2x00_fo_get_target_data(EXT_IOCTL *pext,
    FO_TARGET_DATA_INPUT *bp, int mode);
static int qla2x00_std_get_tgt(scsi_qla_host_t *, EXT_IOCTL *,
    FO_DEVICE_DATA *);
static int qla2x00_fo_get_tgt(mp_host_t *, scsi_qla_host_t *, EXT_IOCTL *,
    FO_DEVICE_DATA *);
static int qla2x00_fo_set_target_data(EXT_IOCTL *pext,
    FO_TARGET_DATA_INPUT *bp, int mode);
static int qla2x00_fo_get_lbtype(EXT_IOCTL *pext, int mode);
static int qla2x00_fo_set_lbtype(EXT_IOCTL *pext, int mode);


STATIC BOOL qla2x00_port_name_in_list(uint8_t *, portname_list *);
STATIC int qla2x00_add_to_portname_list(uint8_t *, portname_list **);
STATIC void qla2x00_free_portname_list(portname_list **);

int qla2x00_lookup_sense_code(unsigned char *sense_buffer);

/*
 * Error structure
 */
struct error_code_info {
	uint8_t	key;
	uint8_t	asc;
	uint8_t	ascq;
	uint8_t	reserved;
};

static  struct error_code_info cfg_sense_code_list[] = {
	{HARDWARE_ERROR, 0x80, 0x02},
	{HARDWARE_ERROR, 	0x44, 0x00},
	{HARDWARE_ERROR,   0x80, 0x03},
	{HARDWARE_ERROR,	0x95, 0x01},
	{HARDWARE_ERROR,	0x45, 0x00},
	{HARDWARE_ERROR,	0xD1, 0x0a},
	{HARDWARE_ERROR,	0x4b, 0x00},
	{HARDWARE_ERROR,	0xd0, 0x06},
	{HARDWARE_ERROR,    0x40, 0x81},
	{HARDWARE_ERROR,	0x44, 0x00},        
	{HARDWARE_ERROR,	0xc0, 0x00},              
	{HARDWARE_ERROR,	0x40, 0x81},
	{HARDWARE_ERROR,	0x44, 0x00},
	{HARDWARE_ERROR,	0xc0, 0x00}, 
	{HARDWARE_ERROR,	0x91, 0x09},
	{HARDWARE_ERROR,	0x40, 0x91},
	{HARDWARE_ERROR,	0x40, 0x92},
	{HARDWARE_ERROR,	0x40, 0x93},
	{HARDWARE_ERROR,	0x40, 0x94},
	{HARDWARE_ERROR,	0x40, 0x95},
	{HARDWARE_ERROR,	0x40, 0x96},
	{HARDWARE_ERROR,   0x44, 0x00},
	{HARDWARE_ERROR,	0x40, 0x81},
	{HARDWARE_ERROR,   0x87, 0x08},
	{HARDWARE_ERROR,  0x44, 0x00},
	{HARDWARE_ERROR,  0xa8, 0x00},
	{HARDWARE_ERROR,  0xa8, 0x01},
	{HARDWARE_ERROR,  0x40, 0x80},
	{HARDWARE_ERROR, 0x3F, 0xA1},
	{HARDWARE_ERROR, 0x0c, 0x80 },
	{HARDWARE_ERROR, 0x0c, 0x00 },
	{HARDWARE_ERROR, 0x0c, 0x81 },
	{0, 0, 0 }
};


int qla2x00_lookup_sense_code(unsigned char *sense_buffer)
{
	int i = 0;

	DEBUG3(printk("%s entered\n",__func__);)

	for (i = 0; 1; i++) {
		if (cfg_sense_code_list[i].key == 0)
			return 0;

		if( cfg_sense_code_list[i].key != (sense_buffer[2] & 0xf) )
			continue;

		if (cfg_sense_code_list[i].asc == sense_buffer[12] &&
			  cfg_sense_code_list[i].ascq == sense_buffer[13] )
			return 1;
	}
}

/*
 * qla2x00_get_hba
 *	Searches the hba structure chain for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	inst = adapter instance number.
 *
 * Returns:
 *	Return value is a pointer to the adapter structure or
 *      NULL if instance not found.
 *
 * Context:
 *	Kernel context.
 */
scsi_qla_host_t *
qla2x00_get_hba(int instance)
{
	scsi_qla_host_t * hbap;

	hbap = (scsi_qla_host_t *) qla2x00_hostlist;

	while (hbap != NULL) {
		if (hbap->instance == instance) {
			break;
		}
		hbap = (scsi_qla_host_t *)hbap->next;
	}
	return hbap;
}

/*
 * qla2x00_fo_stats
 *	Searches the hba structure chan for the requested instance
 *      aquires the mutex and returns a pointer to the hba structure.
 *
 * Input:
 *	stat_p = Pointer to FO_HBA_STAT union.
 *      reset  = Flag, TRUE = reset statistics.
 *                     FALSE = return statistics values.
 *
 * Returns:
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_stats(FO_HBA_STAT *stat_p, BOOL reset)
{
	int32_t	inst, idx;
	uint32_t rval = 0;
	scsi_qla_host_t *hbap;

	DEBUG9(printk("%s: entered.\n", __func__);)

	inst = stat_p->input.HbaInstance;
	stat_p->info.HbaCount = 0;

	hbap = (scsi_qla_host_t *) qla2x00_hostlist;

	while (hbap != NULL) {
		if (inst == FO_ADAPTER_ALL) {
			stat_p->info.HbaCount++;
			idx = hbap->instance;
		} else if (hbap->instance == inst) {
			stat_p->info.HbaCount = 1;
			idx = inst;
		}
		if (reset == TRUE) {
			DEBUG9(printk("%s: reset stats.\n", __func__);)
			hbap->IosRequested = 0;
			hbap->BytesRequested = 0;
			hbap->IosExecuted = 0;
			hbap->BytesExecuted = 0;
		} else {
			DEBUG9(printk("%s: get stats for inst %d.\n",
			    __func__, inst);)

#if 0
			stat_p->info.StatEntry[idx].IosRequested =
				hbap->IosRequested;
			stat_p->info.StatEntry[idx].BytesRequested =
				hbap->BytesRequested;
			stat_p->info.StatEntry[idx].IosExecuted =
				hbap->IosExecuted;
			stat_p->info.StatEntry[idx].BytesExecuted =
				hbap->BytesExecuted;
#endif
		}
		if (inst != FO_ADAPTER_ALL)
			break;
		else
			hbap = (scsi_qla_host_t *)hbap->next;
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return rval;
}

/*
 * qla2x00_fo_get_lun_data
 *      Get lun data from all devices attached to a HBA (FO_GET_LUN_DATA).
 *      Gets lun mask if failover not enabled.
 *
 * Input:
 *      ha = pointer to adapter
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_lun_data(EXT_IOCTL *pext, FO_LUN_DATA_INPUT *bp, int mode)
{
	scsi_qla_host_t  *ha;
	struct list_head	*fcports;
	fc_port_t        *fcport;
	int              ret = 0;
	mp_host_t        *host = NULL;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	os_tgt_t         *ostgt;
	uint8_t          path_id;
	uint16_t         dev_no;
	uint16_t         cnt;
	uint16_t         lun;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;
	FO_LUN_DATA_LIST *u_list, *list;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((int)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)
	DEBUG4(printk("%s: hba %p, buff %p bp->HbaInstance(%x).\n",
	    __func__, ha, bp, (int)bp->HbaInstance));

	if (ha->flags.failover_enabled) {
		if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
			if (!list_empty(&ha->fcports)) {
				/*
				 * Since all ports are unconfigured, return
				 * a dummy entry for each of them.
				 */
				if ((list = (FO_LUN_DATA_LIST *)kmem_zalloc(
				    sizeof(FO_LUN_DATA_LIST), GFP_ATOMIC,
				    12)) == NULL) {
					DEBUG2_9_10(printk("%s: failed to "
					    "alloc memory of size (%ld)\n",
					    __func__,
					    (ulong)sizeof(FO_LUN_DATA_LIST));)
					pext->Status = EXT_STATUS_NO_MEMORY;
					return (-ENOMEM);
				}

				entry = &list->DataEntry[0];

				u_list = (FO_LUN_DATA_LIST *)pext->ResponseAdr;
				u_entry = &u_list->DataEntry[0];

				list_for_each_entry(fcport, &ha->fcports,
				    list) {
					if (fcport->port_type != FCT_TARGET)
						continue;
	
					memcpy(entry->NodeName,
					    fcport->node_name,
					    EXT_DEF_WWN_NAME_SIZE);
					memcpy(entry->PortName,
					    fcport->port_name,
					    EXT_DEF_WWN_NAME_SIZE);

					entry->TargetId = 0;

					for (lun = 0; lun < MAX_LUNS; lun++) {
						entry->Data[lun] = 0;
					}

					DEBUG9(printk("%s(%ld): entry %d for "
					    "unconfigured portname=%02x%02x"
					    "%02x%02x%02x%02x%02x%02x, "
					    "tgt_id=%d.\n",
					    __func__, ha->host_no,
					    list->EntryCount,
					    entry->PortName[0],
					    entry->PortName[1],
					    entry->PortName[2],
					    entry->PortName[3],
					    entry->PortName[4],
					    entry->PortName[5],
					    entry->PortName[6],
					    entry->PortName[7],
					    entry->TargetId);)

					list->EntryCount++;

					ret = copy_to_user(u_entry, entry,
					    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
					if (ret) {
						/* error */
						DEBUG2_9_10(printk(
						    "%s: u_entry %p copy out "
						    "err. EntryCount=%d.\n",
						    __func__, u_entry,
						    list->EntryCount);)
						pext->Status =
						    EXT_STATUS_COPY_ERR;
						break;
					}

					u_entry++;
				}

				KMEM_FREE(list, sizeof(FO_LUN_DATA_LIST));

			} else {
				DEBUG2_9_10(printk(
				    "%s: no HOST for ha inst %ld.\n",
				    __func__, ha->instance);)
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			}

			return (ret);
		}
	}

	if ((list = (FO_LUN_DATA_LIST *)kmem_zalloc(sizeof(FO_LUN_DATA_LIST),
	    GFP_ATOMIC, 12)) == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	entry = &list->DataEntry[0];

	u_list = (FO_LUN_DATA_LIST *)pext->ResponseAdr;
	u_entry = &u_list->DataEntry[0];

	/* find the correct fcport list */
	if (!ha->flags.failover_enabled)
		fcports = &ha->fcports;
	else
		fcports = host->fcports;

	/* Check thru this adapter's fcport list */
	fcport = NULL;
	list_for_each_entry(fcport, fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		if ((atomic_read(&fcport->state) != FC_ONLINE) &&
		    !qla2x00_is_fcport_in_config(ha, fcport)) {
			/* no need to report */
			DEBUG2_9_10(printk("%s(%ld): not reporting fcport "
			    "%02x%02x%02x%02x%02x%02x%02x%02x. state=%i,"
			    " flags=%02x.\n",
			    __func__, ha->host_no, fcport->port_name[0],
			    fcport->port_name[1], fcport->port_name[2],
			    fcport->port_name[3], fcport->port_name[4],
			    fcport->port_name[5], fcport->port_name[6],
			    fcport->port_name[7], atomic_read(&fcport->state),
			    fcport->flags);)
			continue;
		}

		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		/* Return dummy entry for unconfigured ports */
		if (fcport->mp_byte & MP_MASK_UNCONFIGURED) {

			for (lun = 0; lun < MAX_LUNS; lun++) {
				entry->Data[lun] = 0;
			}
			entry->TargetId = 0;

			DEBUG9(printk("%s(%ld): entry %d for unconfigured "
			    "portname=%02x%02x%02x%02x%02x%02x%02x%02x, "
			    "tgt_id=%d.\n",
			    __func__, ha->host_no,
			    list->EntryCount,
			    entry->PortName[0], entry->PortName[1],
			    entry->PortName[2], entry->PortName[3],
			    entry->PortName[4], entry->PortName[5],
			    entry->PortName[6], entry->PortName[7],
			    entry->TargetId);)

			list->EntryCount++;

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s: u_entry %p "
				    "copy out err. EntryCount=%d.\n",
				    __func__, u_entry, list->EntryCount);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;

			continue;
		}

		if (!ha->flags.failover_enabled) {
			/*
			 * Failover disabled. Just return LUN mask info
			 * in lun data entry of this port.
			 */
			memcpy(entry->NodeName,
			    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
			entry->TargetId = 0;

			for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
				if (!(ostgt = ha->otgt[cnt])) {
					continue;
				}

				if (ostgt->vis_port == fcport) {
					entry->TargetId = cnt;
					break;
				}
			}
			if (cnt == MAX_FIBRE_DEVICES) {
				/* Not found?  For now just go to next port. */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_10)
				uint8_t          *tmp_name;
#if USE_PORTNAME
				tmp_name = fcport->port_name;
#else
				tmp_name = fcport->node_name;
#endif

				printk("%s(%ld): ERROR - port "
				    "%02x%02x%02x%02x%02x%02x%02x%02x "
				    "not configured.\n",
				    __func__, ha->host_no,
				    tmp_name[0], tmp_name[1], tmp_name[2],
				    tmp_name[3], tmp_name[4], tmp_name[5],
				    tmp_name[6], tmp_name[7]);
#endif /* DEBUG */

				continue;
			}

			/* Got a valid port */
			list->EntryCount++;

			for (lun = 0; lun < MAX_LUNS; lun++) {
				/* set MSB if masked */
				entry->Data[lun] = LUN_DATA_PREFERRED_PATH;
				if (!EXT_IS_LUN_BIT_SET(&(fcport->lun_mask),
				    lun))
					entry->Data[lun] |= LUN_DATA_ENABLED;
			}

			DEBUG9(printk("%s: got lun_mask for tgt %d\n",
			    __func__, cnt);)
			DEBUG9(qla2x00_dump_buffer((char *)&(fcport->lun_mask),
			    sizeof(lun_bit_mask_t));)

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));

			if (ret) {
				/* error */
				DEBUG9_10(printk("%s: u_entry %p copy "
				    "error. list->EntryCount=%d.\n",
				    __func__, u_entry, list->EntryCount);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			/* Go to next port */
			u_entry++;
			continue;
		}

		/*
		 * Failover is enabled. Go through the mp_devs list and return
		 * lun data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;
			if( dp->mpdev ) {
				dp = dp->mpdev;
			} 

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path = path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				/* Got an entry */
				if (fcport->flags & FC_XP_DEVICE) {
					memcpy(entry->NodeName,
					    dp->nodename,
					    EXT_DEF_WWN_NAME_SIZE);
				} else {
					memcpy(entry->NodeName,
					    fcport->node_name,
					    EXT_DEF_WWN_NAME_SIZE);
				}

				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;
				list->EntryCount++;

				DEBUG9_10(printk(
				    "%s(%ld): got lun_mask for tgt %d\n",
				    __func__, ha->host_no, entry->TargetId);)
				DEBUG9(qla2x00_dump_buffer(
				    (char *)&(fcport->lun_mask),
				    sizeof(lun_bit_mask_t));)

				for (lun = 0; lun < MAX_LUNS; lun++) {
					entry->Data[lun] =
					    path->lun_data.data[lun];
				}

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "copy out err. EntryCount=%d.\n",
					    __func__, u_entry,
					    list->EntryCount);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;

				DEBUG9_10(printk("%s: get_lun_data for tgt %d- "
				    "u_entry(%p) - lun entry[%d] :\n",
				    __func__, entry->TargetId,
				    u_entry,list->EntryCount - 1);)

				DEBUG9(qla2x00_dump_buffer((void *)entry, 64);)

				/*
				 * We found the right path for this port.
				 * Continue with next port.
				 */
				break;
			}

			/* Continue with next port. */
			break;
		}
	}

	DEBUG9(printk("%s: get_lun_data - entry count = [%d]\n",
	    __func__, list->EntryCount);)
	DEBUG4(printk("%s: get_lun_data - entry count = [%d]\n",
	    __func__, list->EntryCount);)

	if (ret == 0) {
		/* copy number of entries */
		ret = copy_to_user(&u_list->EntryCount, &list->EntryCount,
		    sizeof(list->EntryCount));
		pext->ResponseLen = FO_LUN_DATA_LIST_MAX_SIZE;
	}

	KMEM_FREE(list, sizeof(FO_LUN_DATA_LIST));

	DEBUG9(printk("%s: exiting. ret=%d.\n", __func__, ret);)

	return ret;
}

/*
 * qla2x00_fo_set_lun_data
 *      Set lun data for the specified device on the attached hba
 *      (FO_SET_LUN_DATA).
 *      Sets lun mask if failover not enabled.
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_lun_data(EXT_IOCTL *pext, FO_LUN_DATA_INPUT  *bp, int mode)
{
	scsi_qla_host_t  *ha;
	fc_port_t        *fcport;
	int              i;
	int              ret = 0;
	mp_host_t        *host = NULL;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	os_tgt_t         *ostgt;
	uint8_t	         path_id;
	uint16_t         dev_no;
	uint16_t         lun;
	FO_LUN_DATA_LIST *u_list, *list;
	FO_EXTERNAL_LUN_DATA_ENTRY *u_entry, *entry;

	typedef struct _tagStruct {
		FO_LUN_DATA_INPUT   foLunDataInput;
		FO_LUN_DATA_LIST    foLunDataList;
	}
	com_struc;
	com_struc *com_iter;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((int)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (ha->flags.failover_enabled)
		if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
			DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			return (ret);
		}

	if ((list = (FO_LUN_DATA_LIST *)kmem_zalloc(sizeof(FO_LUN_DATA_LIST),
	    GFP_ATOMIC, 13)) == NULL) {

		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	entry = &list->DataEntry[0];

	/* get lun data list from user */
	com_iter = (com_struc *)pext->RequestAdr;
	u_list = &(com_iter->foLunDataList);
	u_entry = &u_list->DataEntry[0];

	ret = copy_from_user(list, u_list, sizeof(FO_LUN_DATA_LIST));
	if (ret) {
		/* error */
		DEBUG2_9_10(printk("%s: u_list %p copy error.\n",
		    __func__, u_list);)
		pext->Status = EXT_STATUS_COPY_ERR;
		KMEM_FREE(list, FO_LUN_DATA_LIST);
		return (ret);
	}

	DEBUG2(printk("qla_fo_set_lun_data: pext->RequestAdr(%p) u_list (%p) "
			"sizeof(FO_LUN_DATA_INPUT) =(%d) and 64 bytes...\n",
			pext->RequestAdr, u_list,
			(int)sizeof(FO_LUN_DATA_INPUT));)
	DEBUG2(qla2x00_dump_buffer((void *)u_list, 64);)

	for (i = 0; i < list->EntryCount; i++, u_entry++) {

		ret = copy_from_user(entry, u_entry,
		    sizeof(FO_EXTERNAL_LUN_DATA_ENTRY));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p copy error.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		if (!ha->flags.failover_enabled) {
			/*
			 * Failover disabled. Just find the port and set
			 * LUN mask values in lun_mask field of this port.
			 */

			if (entry->TargetId >= MAX_FIBRE_DEVICES)
				/* ERROR */
				continue;

			if (!(ostgt = ha->otgt[entry->TargetId]))
				/* ERROR */
				continue;

			if (!(fcport = ostgt->vis_port))
				/* ERROR */
				continue;

			for (lun = 0; lun < MAX_LUNS; lun++) {
				/* set MSB if masked */
				if (entry->Data[lun] | LUN_DATA_ENABLED) {
					EXT_CLR_LUN_BIT(&(fcport->lun_mask),
								lun);
				} else {
					EXT_SET_LUN_BIT(&(fcport->lun_mask),
								lun);
				}
			}

			/* Go to next entry */
			continue;
		}

		/*
		 * Failover is enabled. Go through the mp_devs list and set lun
		 * data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
					continue;

			if ((pathlist = dp->path_list) == NULL)
					continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path = path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				for (lun = 0; lun < MAX_LUNS; lun++) {
					path->lun_data.data[lun] =
					    entry->Data[lun];
					DEBUG4(printk("cfg_set_lun_data: lun "
					    "data[%d] = 0x%x \n", lun,
					    path->lun_data.data[lun]);)
				}

				break;
			}
			break;
		}
	}

	KMEM_FREE(list, FO_LUN_DATA_LIST);

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return ret;
}

/*
 * qla2x00_fo_get_target_data
 *      Get the target control byte for all devices attached to a HBA.
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_target_data(EXT_IOCTL *pext, FO_TARGET_DATA_INPUT *bp, int mode)
{
	scsi_qla_host_t  *ha;
	int              ret = 0;
	mp_host_t        *host = NULL;
	FO_DEVICE_DATA   *entry;


	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((int)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (ha->flags.failover_enabled)
		if ((host = qla2x00_cfg_find_host(ha)) == NULL &&
		    list_empty(&ha->fcports)) {
			DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			return (ret);
		}

	if ((entry = (FO_DEVICE_DATA *)kmem_zalloc(sizeof(FO_DEVICE_DATA),
	    GFP_ATOMIC,14)) == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_DEVICE_DATA));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	/* Return data accordingly. */
	if (!ha->flags.failover_enabled)
		ret = qla2x00_std_get_tgt(ha, pext, entry);
	else
		ret = qla2x00_fo_get_tgt(host, ha, pext, entry);


	if (ret == 0) {
		pext->ResponseLen = sizeof(FO_DEVICE_DATABASE);
	}

	KMEM_FREE(entry, sizeof(FO_DEVICE_DATA));

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return (ret);
}

static int
qla2x00_std_get_tgt(scsi_qla_host_t *ha, EXT_IOCTL *pext, FO_DEVICE_DATA *entry)
{
	int 		ret = 0;
	uint16_t 	i, cnt;
	uint32_t	b;

	fc_port_t 	*fcport;
	os_tgt_t 	*ostgt;

	FO_DEVICE_DATA  *u_entry;

	DEBUG9(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	u_entry = (FO_DEVICE_DATA *) pext->ResponseAdr;

	if (pext->ResponseLen < sizeof(FO_DEVICE_DATA)) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		DEBUG9_10(printk("%s: ERROR ResponseLen %d too small.\n",
		    __func__, pext->ResponseLen);)

		return (ret);
	}

	DEBUG9(printk("%s(%ld): user buffer size=%d. Copying fcport list\n",
	    __func__, ha->host_no, pext->ResponseLen);)

	/* Loop through and return ports found. */
	/* Check thru this adapter's fcport list */
	i = 0;
	fcport = NULL;
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;
	
		if (i >= MAX_TARGETS)
			break;

		/* clear for a new entry */
		memset(entry, 0, sizeof(FO_DEVICE_DATA));

		memcpy(entry->WorldWideName,
		    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		for (b = 0; b < 3 ; b++)
			entry->PortId[b] = fcport->d_id.r.d_id[2-b];

		DEBUG9(printk("%s(%ld): found fcport %p:%02x%02x%02x%02x"
		    "%02x%02x%02x%02x.\n",
		    __func__, ha->host_no,
		    fcport,
		    fcport->port_name[0],
		    fcport->port_name[1],
		    fcport->port_name[2],
		    fcport->port_name[3],
		    fcport->port_name[4],
		    fcport->port_name[5],
		    fcport->port_name[6],
		    fcport->port_name[7]);)

		/*
		 * Just find the port and return target info.
		 */
		for (cnt = 0; cnt < MAX_FIBRE_DEVICES; cnt++) {
			if (!(ostgt = ha->otgt[cnt])) {
				continue;
			}

			if (ostgt->vis_port == fcport) {
				DEBUG9(printk("%s(%ld): Found target %d.\n",
				    __func__, ha->host_no, cnt);)

				entry->TargetId = cnt;
				break;
			}
		}

		if (cnt == MAX_FIBRE_DEVICES) {
			/* Not found?  This target could be unconfigured. */
			/* Print error message since this should not happen. */
#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_9) || defined(QL_DEBUG_LEVEL_10)
			uint8_t          *tmp_name;
#if USE_PORTNAME
			tmp_name = fcport->port_name;
#else
			tmp_name = fcport->node_name;
#endif

			printk("%s(%ld): ERROR no target for "
			    "port %02x%02x%02x%02x%02x%02x%02x%02x, "
			    "return not configured.\n",
			    __func__, ha->host_no,
			    tmp_name[0], tmp_name[1], tmp_name[2],
			    tmp_name[3], tmp_name[4], tmp_name[5],
			    tmp_name[6], tmp_name[7]);
#endif /* DEBUG */

			entry->MultipathControl = MP_MASK_UNCONFIGURED;
		} else {
			entry->MultipathControl = 0; /* always configured */
		}

		ret = copy_to_user(u_entry, entry, sizeof(FO_DEVICE_DATA));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s(%ld): u_entry %p copy "
			    "out err. tgt id=%d.\n",
			    __func__, ha->host_no, u_entry, cnt);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		u_entry++;
	}

	DEBUG9(printk("%s(%ld): done copying fcport list entries.\n",
	    __func__, ha->host_no);)

	DEBUG9(printk("%s(%ld): exiting. ret = %d.\n",
	    __func__, ha->host_no, ret);)

	return (ret);
} /* qla2x00_get_std_tgt */

static int
qla2x00_fo_get_tgt(mp_host_t *host, scsi_qla_host_t *ha,
    EXT_IOCTL *pext, FO_DEVICE_DATA *entry)
{
	int		ret = 0;
	uint8_t 	path_id;
	uint16_t	dev_no;
	uint32_t	b;
	uint16_t	cnt = 0;

	fc_port_t	*fcport;
	mp_device_t	*dp;
	mp_path_list_t	*pathlist;
	mp_path_t	*path;

	FO_DEVICE_DATA	*u_entry;

	DEBUG9(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	u_entry = (FO_DEVICE_DATA *) pext->ResponseAdr;

	/* If host is NULL then report all online fcports of the corresponding
	 * ha as unconfigured devices.  ha should never be NULL.
	 */
	if (host == NULL) {
		/* Loop through and return ports found. */
		/* Check thru this adapter's fcport list */
		cnt = 0;
		fcport = NULL;
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->port_type != FCT_TARGET)
				continue;
	
			if (atomic_read(&fcport->state) != FC_ONLINE) {
				/* no need to report */
				DEBUG2_9_10(printk("%s(%ld): not reporting "
				    "fcport %02x%02x%02x%02x%02x%02x%02x%02x. "
				    "state=%i, flags=%02x.\n",
				    __func__, ha->host_no, fcport->port_name[0],
				    fcport->port_name[1], fcport->port_name[2],
				    fcport->port_name[3], fcport->port_name[4],
				    fcport->port_name[5], fcport->port_name[6],
				    fcport->port_name[7],
				    atomic_read(&fcport->state),
				    fcport->flags);)
				continue;
			}

			cnt++;
			if (cnt >= MAX_TARGETS)
				break;

			/* clear for a new entry */
			memset(entry, 0, sizeof(FO_DEVICE_DATA));

			memcpy(entry->WorldWideName,
			    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);
			memcpy(entry->PortName,
			    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

			DEBUG10(printk("%s(%ld): found fcport %p:%02x%02x%02x"
			    "%02x%02x%02x%02x%02x.\n",
			    __func__, ha->host_no,
			    fcport,
			    fcport->port_name[0],
			    fcport->port_name[1],
			    fcport->port_name[2],
			    fcport->port_name[3],
			    fcport->port_name[4],
			    fcport->port_name[5],
			    fcport->port_name[6],
			    fcport->port_name[7]);)

			for (b = 0; b < 3 ; b++)
				entry->PortId[b] = fcport->d_id.r.d_id[2-b];

			DEBUG9_10(printk("%s(%ld): fcport mpbyte=%02x. "
			    "return unconfigured. ",
			    __func__, ha->host_no, fcport->mp_byte);)

			entry->TargetId = 0;
			entry->Dev_No = 0;
			entry->MultipathControl = MP_MASK_UNCONFIGURED;

			DEBUG9_10(printk("tgtid=%d dev_no=%d, mpdata=0x%x.\n",
			    entry->TargetId, entry->Dev_No,
			    entry->MultipathControl);)

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "copy out err. no tgt id.\n",
				    __func__, ha->host_no, u_entry);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;
		}

		DEBUG9(printk("%s(%ld): after returning unconfigured fcport "
		    "list. got %d entries.\n",
		    __func__, ha->host_no, cnt);)

		return (ret);
	}

	/* Check thru fcport list on host */
	/* Loop through and return online ports found. */
	/* Check thru this adapter's fcport list */
	cnt = 0;
	fcport = NULL;
	list_for_each_entry(fcport, host->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;
	
		if ((atomic_read(&fcport->state) != FC_ONLINE) &&
		    !qla2x00_is_fcport_in_config(ha, fcport)) {
			/* no need to report */
			DEBUG2_9_10(printk("%s(%ld): not reporting "
			    "fcport %02x%02x%02x%02x%02x%02x%02x%02x. "
			    "state=%i, flags=%02x.\n",
			    __func__, ha->host_no, fcport->port_name[0],
			    fcport->port_name[1], fcport->port_name[2],
			    fcport->port_name[3], fcport->port_name[4],
			    fcport->port_name[5], fcport->port_name[6],
			    fcport->port_name[7],
			    atomic_read(&fcport->state),
			    fcport->flags);)
			continue;
		}

		cnt++;
		if (cnt >= MAX_TARGETS)
			break;

		/* clear for a new entry */
		memset(entry, 0, sizeof(FO_DEVICE_DATA));

		memcpy(entry->PortName,
		    fcport->port_name, EXT_DEF_WWN_NAME_SIZE);

		DEBUG10(printk("%s(%ld): found fcport %p:%02x%02x%02x%02x"
		    "%02x%02x%02x%02x.\n",
		    __func__, host->ha->host_no,
		    fcport,
		    fcport->port_name[0],
		    fcport->port_name[1],
		    fcport->port_name[2],
		    fcport->port_name[3],
		    fcport->port_name[4],
		    fcport->port_name[5],
		    fcport->port_name[6],
		    fcport->port_name[7]);)

		for (b = 0; b < 3 ; b++)
			entry->PortId[b] = fcport->d_id.r.d_id[2-b];

		if (fcport->mp_byte & MP_MASK_UNCONFIGURED) {
			DEBUG9_10(printk("%s(%ld): fcport mpbyte=%02x. "
			    "return unconfigured. ",
			    __func__, host->ha->host_no, fcport->mp_byte);)
			printk(KERN_INFO "%s(%ld): fcport mpbyte=%02x. "
			    "return unconfigured. ",
			    __func__, host->ha->host_no, fcport->mp_byte);

			memcpy(entry->WorldWideName,
			    fcport->node_name, EXT_DEF_WWN_NAME_SIZE);

			entry->TargetId = fcport->dev_id;
			entry->Dev_No = 0;
			entry->MultipathControl = MP_MASK_UNCONFIGURED;

			DEBUG9_10(printk("tgtid=%d dev_no=%d, mpdata=0x%x.\n",
			    entry->TargetId, entry->Dev_No,
			    entry->MultipathControl);)

			ret = copy_to_user(u_entry, entry,
			    sizeof(FO_DEVICE_DATA));
			if (ret) {
				/* error */
				DEBUG2_9_10(printk("%s(%ld): u_entry %p "
				    "copy out err. tgt id=%d.\n",
				    __func__, host->ha->host_no, u_entry,
				    fcport->dev_id);)
				pext->Status = EXT_STATUS_COPY_ERR;
				break;
			}

			u_entry++;
			continue;
		}

		/*
		 * Port was configured. Go through the mp_devs list and
		 * get target data in configured path.
		 */
		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			if (dp->mpdev) {
			        dp = dp->mpdev;
			}

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path= path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				if (fcport->flags & FC_XP_DEVICE) {
					memcpy(entry->WorldWideName,
					    dp->nodename,
					    EXT_DEF_WWN_NAME_SIZE);
					DEBUG4(printk(KERN_INFO "%s XP device"
					    " :copy the node name from " 
					    "mp_dev:%0x\n",__func__,
					    dp->nodename[7]);)
				} else {
					memcpy(entry->WorldWideName,
					    fcport->node_name,
					    EXT_DEF_WWN_NAME_SIZE);
					DEBUG4(printk(KERN_INFO "%s :" 
					    " copy the node name from " 
					    " fcport:%0x\n",__func__, 
					      dp->nodename[7]);)
				}

				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;

				if (path->config == TRUE ||
				    !mp_config_required) {
					entry->MultipathControl = path->mp_byte;
				} else {
					entry->MultipathControl =
					    MP_MASK_UNCONFIGURED;
				}

				DEBUG9_10(printk("%s(%ld): fcport path->id "
				    "= %d, target/mpbyte data = 0x%02x.\n",
				    __func__, host->ha->host_no,
				    path->id, entry->MultipathControl);)

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s(%ld): u_entry %p "
					    "copy out err. tgt id=%d.\n",
					    __func__, host->ha->host_no,
					    u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;

				/* Path found. Continue with next fcport */
				break;
			}
			break;
		}
	}

	DEBUG9(printk("%s(%ld): after checking fcport list. got %d entries.\n",
	    __func__, host->ha->host_no, cnt);)

	/* For ports not found but were in config file, return configured
	 * status so agent will try to issue commands to it and GUI will display
	 * them as missing.
	 */
	for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		/* Sanity check */
		if (qla2x00_is_ww_name_zero(dp->nodename))
			continue;

		if ((pathlist = dp->path_list) == NULL)
			continue;

		path = pathlist->last;
		for (path_id = 0; path_id < pathlist->path_cnt;
		    path_id++, path = path->next) {

			/* Sanity check */
			if (qla2x00_is_ww_name_zero(path->portname))
				continue;

			if (path->port == NULL) {
				if (path->host != host) {
					/* path on other host. don't report */
					DEBUG10(printk("%s(%ld): path host %p "
					    "not for current host %p.\n",
					    __func__, host->ha->host_no,
					    path->host, host);)

					continue;
				}

				/* clear for a new entry */
				memset(entry, 0, sizeof(FO_DEVICE_DATA));

				/* This device was not found. Return
				 * unconfigured.
				 */
				memcpy(entry->WorldWideName,
				    dp->nodename, EXT_DEF_WWN_NAME_SIZE);
				memcpy(entry->PortName,
				    path->portname, EXT_DEF_WWN_NAME_SIZE);

				entry->TargetId = dp->dev_id;
				entry->Dev_No = path->id;
				entry->MultipathControl = path->mp_byte;
				cnt++;

				DEBUG9_10(printk("%s: found missing device. "
				    "return tgtid=%d dev_no=%d, mpdata=0x%x for"
				    " port %02x%02x%02x%02x%02x%02x%02x%02x\n",
				    __func__, entry->TargetId, entry->Dev_No,
				    entry->MultipathControl,
				    path->portname[0], path->portname[1],
				    path->portname[2], path->portname[3],
				    path->portname[4], path->portname[5],
				    path->portname[6], path->portname[7]);)

				ret = copy_to_user(u_entry, entry,
				    sizeof(FO_DEVICE_DATA));
				if (ret) {
					/* error */
					DEBUG2_9_10(printk("%s: u_entry %p "
					    "copy out err. tgt id=%d.\n",
					    __func__, u_entry, dp->dev_id);)
					pext->Status = EXT_STATUS_COPY_ERR;
					break;
				}

				u_entry++;
			}
		}
	}

	DEBUG9(printk("%s(%ld): after checking missing devs. got %d entries.\n",
	    __func__, host->ha->host_no, cnt);)

	DEBUG9(printk("%s(%ld): exiting. ret = %d.\n",
	    __func__, host->ha->host_no, ret);)

	return (ret);

} /* qla2x00_get_fo_tgt */

/*
 * qla2x00_fo_set_target_data
 *      Set multipath control byte for all devices on the attached hba
 *
 * Input:
 *      bp = pointer to buffer
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_target_data(EXT_IOCTL *pext, FO_TARGET_DATA_INPUT  *bp, int mode)
{
	scsi_qla_host_t  *ha;
	int              i;
	int              ret = 0;
	mp_host_t        *host;
	mp_device_t      *dp;
	mp_path_t        *path;
	mp_path_list_t   *pathlist;
	uint16_t         dev_no;
	uint8_t	         path_id;
	FO_DEVICE_DATA *entry, *u_entry;

	DEBUG9(printk("%s: entered.\n", __func__);)

	ha = qla2x00_get_hba((int)bp->HbaInstance);

	if (!ha) {
		DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
		    __func__, bp->HbaInstance);)

		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	DEBUG9(printk("%s: ha inst %ld, buff %p.\n",
	    __func__, ha->instance, bp);)

	if (!ha->flags.failover_enabled)
		/* non-failover mode. nothing to be done. */
		return 0;


	if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
		DEBUG2_9_10(printk("%s: no HOST for ha inst %ld.\n",
		    __func__, ha->instance);)
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	if ((entry = (FO_DEVICE_DATA *)kmem_zalloc(sizeof(FO_DEVICE_DATA),
	    GFP_ATOMIC,15)) == NULL) {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_DEVICE_DATA));)
		pext->Status = EXT_STATUS_NO_MEMORY;
		return (-ENOMEM);
	}

	u_entry = (FO_DEVICE_DATA *)(pext->RequestAdr +
	    sizeof(FO_TARGET_DATA_INPUT));

	for (i = 0; i < MAX_TARGETS; i++, u_entry++) {
		ret = copy_from_user(entry, u_entry, sizeof(FO_DEVICE_DATA));
		if (ret) {
			/* error */
			DEBUG2_9_10(printk("%s: u_entry %p copy error.\n",
			    __func__, u_entry);)
			pext->Status = EXT_STATUS_COPY_ERR;
			break;
		}

		for (dev_no = 0; dev_no < MAX_MP_DEVICES; dev_no++) {
			dp = host->mp_devs[dev_no];

			if (dp == NULL)
				continue;

			/* Lookup entry name */
			if (!qla2x00_is_portname_in_device(dp, entry->PortName))
				continue;

			if ((pathlist = dp->path_list) == NULL)
				continue;

			path = pathlist->last;
			for (path_id = 0; path_id < pathlist->path_cnt;
			    path_id++, path= path->next) {

				if (path->host != host)
					continue;

				if (!qla2x00_is_portname_equal(path->portname,
				    entry->PortName))
					continue;

				path->mp_byte = entry->MultipathControl;

				DEBUG9(printk("cfg_set_target_data: %d target "
				    "data = 0x%x \n",
				    path->id,path->mp_byte);)

				/*
				 * If this is the visible path, then make it
				 * available on next reboot.
				 */
				if (!((path->mp_byte & MP_MASK_HIDDEN) ||
				    (path->mp_byte & MP_MASK_UNCONFIGURED))) {
					pathlist->visible = path->id;
				}

				/* Found path. Go to next entry. */
				break;
			}
			break;
		}
	}

	KMEM_FREE(entry, sizeof(FO_DEVICE_DATA));

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)

	return (ret);

}

/*
 * qla2x00_fo_ioctl
 *	Provides functions for failover ioctl() calls.
 *
 * Input:
 *	ha = adapter state pointer.
 *	ioctl_code = ioctl function to perform
 *	arg = Address of application EXT_IOCTL cmd data
 *	mode = flags
 *
 * Returns:
 *	Return value is the ioctl rval_p return value.
 *	0 = success
 *
 * Context:
 *	Kernel context.
 */
/* ARGSUSED */
int
qla2x00_fo_ioctl(scsi_qla_host_t *ha, int ioctl_code, EXT_IOCTL *pext, int mode)
{
	typedef	union {
		FO_PARAMS params;
		FO_GET_PATHS path;
		FO_SET_CURRENT_PATH set_path;
		/* FO_HBA_STAT_INPUT stat; */
		FO_HBA_STAT stat;
		FO_LUN_DATA_INPUT lun_data;
		FO_TARGET_DATA_INPUT target_data;
	} fodata_t;
	fodata_t	*buff;
	int		rval = 0;
	size_t		in_size, out_size;


	ENTER("qla2x00_fo_ioctl");
	DEBUG9(printk("%s: entered. arg (%p):\n", __func__, pext);)

	/*
	 * default case for this switch not needed,
	 * ioctl_code validated by caller.
	 */
	in_size = out_size = 0;
	switch (ioctl_code) {
		case FO_CC_GET_PARAMS:
			out_size = sizeof(FO_PARAMS);
			break;
		case FO_CC_SET_PARAMS:
			in_size = sizeof(FO_PARAMS);
			break;
		case FO_CC_GET_PATHS:
			in_size = sizeof(FO_GET_PATHS);
			break;
		case FO_CC_SET_CURRENT_PATH:
			in_size = sizeof(FO_SET_CURRENT_PATH);
			break;
		case FO_CC_GET_HBA_STAT:
		case FO_CC_RESET_HBA_STAT:
			in_size = sizeof(FO_HBA_STAT_INPUT);
			break;
		case FO_CC_GET_LUN_DATA:
			in_size = sizeof(FO_LUN_DATA_INPUT);
			break;
		case FO_CC_SET_LUN_DATA:
			in_size = sizeof(FO_LUN_DATA_INPUT);
			break;
		case FO_CC_GET_TARGET_DATA:
			in_size = sizeof(FO_TARGET_DATA_INPUT);
			break;
		case FO_CC_SET_TARGET_DATA:
			in_size = sizeof(FO_TARGET_DATA_INPUT);
			break;
		case FO_CC_GET_LBTYPE:
			/* Empty */
			break;
		case FO_CC_SET_LBTYPE:
			/* Empty */
			break;

	}

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&buff,
	    sizeof(fodata_t))) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG10(printk( "%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(fodata_t));)
		goto done_fo_ioctl;
	}

	if (in_size != 0) {
		if ((int)pext->RequestLen < in_size) {
			pext->Status = EXT_STATUS_INVALID_PARAM;
			pext->DetailStatus = EXT_DSTATUS_REQUEST_LEN;
			DEBUG10(printk("%s: got invalie req len (%d).\n",
			    __func__, pext->RequestLen);)

		} else {
			rval = copy_from_user(buff, pext->RequestAdr, in_size);
			if (rval) {
				DEBUG2_9_10(printk("%s: req buf copy error. "
				    "size=%ld.\n",
				    __func__, (ulong)in_size);)

				pext->Status = EXT_STATUS_COPY_ERR;
			} else {
				DEBUG9(printk("qla2x00_fo_ioctl: req buf "
				    "copied ok.\n"));
			}
		}
	} else if (out_size != 0 && (ulong)pext->ResponseLen < out_size) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		pext->DetailStatus = out_size;
		DEBUG10(printk("%s: got invalie resp len (%d).\n",
		    __func__, pext->ResponseLen);)
	}

	if (rval != 0 || pext->Status != 0)
		goto done_fo_ioctl;

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	switch (ioctl_code) {
		case FO_CC_GET_PARAMS:
			DEBUG4(printk("calling qla2x00_fo_get_param\n");)
			rval = qla2x00_fo_get_params(&(buff->params));
			break;
		case FO_CC_SET_PARAMS:
			DEBUG4(printk("calling qla2x00_fo_set_param\n");)
			rval = qla2x00_fo_set_params(&(buff->params));
			break;
		case FO_CC_GET_PATHS:
			DEBUG4(printk("calling qla2x00_fo_get_paths\n");)
			rval = qla2x00_cfg_get_paths(pext,
			    &(buff->path),mode);
			if (rval != 0)
				out_size = 0;
			break;
		case FO_CC_SET_CURRENT_PATH:
			DEBUG4(printk("calling qla2x00_fo_set_paths\n");)
			rval = qla2x00_cfg_set_current_path(pext,
			    &(buff->set_path),mode);
			break;
		case FO_CC_RESET_HBA_STAT:
			DEBUG4(printk("calling qla2x00_fo_reset_hba_stat\n");)
			rval = qla2x00_fo_stats(&(buff->stat), TRUE);
			break;
		case FO_CC_GET_HBA_STAT:
			DEBUG4(printk("calling qla2x00_fo_get_hba_stat\n");)
			rval = qla2x00_fo_stats(&(buff->stat), FALSE);
			break;
		case FO_CC_GET_LUN_DATA:

			DEBUG4(printk("calling qla2x00_fo_get_lun_data\n");)
			DEBUG4(printk("pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_get_lun_data(pext,
			    &(buff->lun_data), mode);

			if (rval != 0)
				out_size = 0;
			break;
		case FO_CC_SET_LUN_DATA:

			DEBUG4(printk("calling qla2x00_fo_set_lun_data\n");)
			DEBUG4(printk("	pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_set_lun_data(pext,
			    &(buff->lun_data), mode);
			break;
		case FO_CC_GET_TARGET_DATA:
			DEBUG4(printk("calling qla2x00_fo_get_target_data\n");)
			DEBUG4(printk("pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)

			rval = qla2x00_fo_get_target_data(pext,
			    &(buff->target_data), mode);

			if (rval != 0) {
				out_size = 0;
			}
			break;
		case FO_CC_SET_TARGET_DATA:
			DEBUG4(printk("calling qla2x00_fo_set_target_data\n");)
			DEBUG4(printk("	pext->RequestAdr (%p):\n",
			    pext->RequestAdr);)
			rval = qla2x00_fo_set_target_data(pext,
			    &(buff->target_data), mode);
			break;
		case FO_CC_GET_LBTYPE:
			DEBUG4(printk("calling qla2x00_fo_get_lbtype\n");)
			rval = qla2x00_fo_get_lbtype(pext, mode);
			break;
		case FO_CC_SET_LBTYPE:
			DEBUG4(printk("calling qla2x00_fo_set_lbtype\n");)
			rval = qla2x00_fo_set_lbtype(pext, mode);
			break;
	}

	if (rval == 0) {
		rval = copy_to_user(pext->ResponseAdr, buff, out_size);
		if (rval) {
			DEBUG10(printk("%s: resp buf copy error. size=%ld.\n",
			    __func__, (ulong)out_size);)
			pext->Status = EXT_STATUS_COPY_ERR;
		}
	}

done_fo_ioctl:

	qla2x00_free_ioctl_scrap_mem(ha);

	if (rval != 0) {
		/*EMPTY*/
		DEBUG10(printk("%s: **** FAILED ****\n", __func__);)
	} else {
		/*EMPTY*/
		DEBUG9(printk("%s: exiting normally\n", __func__);)
	}

	return rval;
}

/*
 * qla2x00_cfg_get_path_cnt
 *	Get the path cnt for the target. 
 * Input:
 *	sp = Pointer to command.
 *	ha = adapter state pointer.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
int qla2x00_cfg_get_path_cnt(scsi_qla_host_t *ha, srb_t *sp)
{
	mp_host_t	*host;			/* host adapter pointer */
	os_tgt_t	*tq;
	mp_path_list_t  *path_list;
	mp_device_t	*dp;
	int	id = 0;

	tq = sp->tgt_queue;
	if ((host = qla2x00_cfg_find_host(ha)) != NULL) {
		if ((dp = qla2x00_find_mp_dev_by_nodename(host,
		    tq->node_name)) != NULL) {
			path_list = dp->path_list;
			id = path_list->path_cnt;
		}
	}
	return id;
}


/*
 * qla2x00_fo_count_retries
 *	Increment the retry counter for the command.
 *      Set or reset the SRB_RETRY flag.
 *
 * Input:
 *	sp = Pointer to command.
 *
 * Returns:
 *	TRUE -- retry
 * 	FALSE -- don't retry
 *
 * Context:
 *	Kernel context.
 */
static BOOL
qla2x00_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp)
{
	BOOL		retry = FALSE;
	os_lun_t	*lq;
	os_tgt_t	*tq;
	scsi_qla_host_t	*vis_ha;
	uint16_t        path_id;
	struct fo_information	*mp_info = NULL;
	int		path_cnt;
	mp_lun_t	*mplun;


	DEBUG9(printk("%s: entered.\n", __func__);)

	lq = sp->lun_queue;
	mp_info = (struct fo_information *) lq->fo_info;

	/* 
	 * if load balancing is enabled then we don't try 
	 * and failover until  all the active paths are gone.
	 */
	mplun = (mp_lun_t *)sp->fclun->mplun; 
	path_cnt = qla2x00_cfg_get_path_cnt(ha, sp);
	DEBUG(printk("%s path_cnt=%d osl_fo_path_cnt=%d for lun=%d\n",
	    __func__, path_cnt, mp_info->path_cnt, lq->fclun->lun);)

       if (test_and_clear_bit(LUN_MPIO_RESET_CNTS, &lq->q_flag)) {
		DEBUG2(printk("%s CLEAR MPIO counts: vis_ha=%ld, max retries %d path_cnt=%d osl_fo_path_cnt=%d for lun=%d\n",
	    __func__, ha->host_no,qla_fo_params.MaxRetriesPerPath,path_cnt, mp_info->path_cnt, lq->fclun->lun);)
               for (path_id = 0; path_id < MAX_PATHS_PER_DEVICE; path_id++)
                       mp_info->fo_retry_cnt[path_id] = 0;
	}

	/* check to see if we have exhausted retries on all the paths */
        for( path_id = 0; path_id < mp_info->path_cnt; path_id++)  {
                if(mp_info->fo_retry_cnt[path_id] >=
                        qla_fo_params.MaxRetriesPerPath)
                                continue;
                retry = TRUE;
                break;
        }

	if(retry == FALSE) {
	DEBUG2(printk("%s vis_ha=%ld, path_cnt=%d osl_fo_path_cnt=%d for lun=%d\n",
	    __func__, ha->host_no,path_cnt, mp_info->path_cnt, lq->fclun->lun);)
                printk(KERN_INFO
                    "qla2x00: no more failovers for request - "
		    "pid= %ld\n", sp->cmd->serial_number);
	} else {
		/*
		 * We haven't exceeded the max retries for this request, check
		 * max retries this path
		 */
		if((++sp->fo_retry_cnt % qla_fo_params.MaxRetriesPerPath) == 0){
			if (mplun) {
				if( mplun->load_balance_type >= LB_LRU ) {
					DEBUG2(printk(" %s: LB-FAILOVER - lun=%d visha=%ld, sp=%p,"
					"pid =%ld, path_id=%d fo retry= %d, act paths=%d max_paths=%d\n",
					 __func__, sp->fclun->lun, ha->host_no, sp,  
					sp->cmd->serial_number, path_id, 
					mp_info->fo_retry_cnt[path_id], mplun->act_cnt,
					path_cnt);)
					if ( qla2x00_del_fclun_from_active_list( mplun, 
						sp->fclun, sp) == 0 ){
						sp->fclun = sp->lun_queue->fclun;
						sp->ha = sp->fclun->fcport->ha;
						return 1;
					}
					printk(KERN_INFO
						"%s: no more active paths for request - "
						"pid= %ld, lun=%d, lq=%p\n", 
						__func__,sp->cmd->serial_number, sp->fclun->lun, lq);
				}
			}

			path_id = sp->fclun->fcport->cur_path;
			mp_info->fo_retry_cnt[path_id]++;
			DEBUG(printk(" %s: FAILOVER - queuing ha=%ld, sp=%p,"
				"pid =%ld, path_id=%d fo retry= %d \n",
				 __func__, ha->host_no, sp, 
				sp->cmd->serial_number, path_id, 
				mp_info->fo_retry_cnt[path_id]);)
			/*
			 * Note: we don't want it to timeout, so it is
			 * recycling on the retry queue and the fialover queue.
			 */
			tq = sp->tgt_queue;
			set_bit(LUN_MPIO_BUSY, &lq->q_flag);

			/*
			 * ??? We can get a path error on any ha, but always
			 * queue failover on originating ha. This will allow us
			 * to syncronized the requests for a given lun.
			 */
			sp->f_start=jiffies;/*ra 10/29/01*/
			/* Now queue it on to be failover */
			sp->ha = ha;
			/* we can only failover using the visible HA */
		 	vis_ha = (scsi_qla_host_t *) sp->cmd->host->hostdata;
			add_to_failover_queue(vis_ha,sp);
		}
	}

	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry ;
}

BOOL
qla2x00_fo_check_device(scsi_qla_host_t *ha, srb_t *sp)
{
	
	BOOL		retry = FALSE;
	os_lun_t	*lq;
	Scsi_Cmnd 	 *cp;
	fc_port_t 	 *fcport;
	
	if ( !(sp->flags & SRB_GOT_SENSE) )
		return retry;

	cp = sp->cmd;
	lq = sp->lun_queue;
	fcport = lq->fclun->fcport;
	switch (cp->sense_buffer[2] & 0xf) {

		case NOT_READY:
			if (fcport->flags & (FC_MSA_DEVICE | FC_EVA_DEVICE 
				| FC_AA_EVA_DEVICE | FC_AA_MSA_DEVICE)) {
				/*
				 * if we can't access port 
				 */
				if (cp->sense_buffer[12] == 0x4 &&
					(cp->sense_buffer[13] == 0x0 ||
					cp->sense_buffer[13] == 0x2)) {
				 	sp->err_id = SRB_ERR_DEVICE;
					CMD_RESULT(sp->cmd) = DID_NO_CONNECT << 16;
					return (TRUE);
				}
			} 
 	     		if (fcport->flags & FC_DSXXX_DEVICE) {
				   /* retry I/O */
				if (cp->sense_buffer[12] == 0x4 &&
				    (cp->sense_buffer[13] == 0x0 ||
					cp->sense_buffer[13] == 0xa)) {
					CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
					return (TRUE);
				}
				if (cp->sense_buffer[12] == 0x4 &&
					cp->sense_buffer[13] == 0xb) {
					sp->err_id = SRB_ERR_DEVICE;
					CMD_RESULT(sp->cmd) = DID_NO_CONNECT << 16;
					return (TRUE);
				}

			} 
			break;

		case UNIT_ATTENTION:
			if (fcport->flags & (FC_EVA_DEVICE | 
				FC_AA_EVA_DEVICE | FC_AA_MSA_DEVICE)) {
				if ((cp->sense_buffer[12] == 0xa &&
					cp->sense_buffer[13] == 0x8)) {
				 	sp->err_id = SRB_ERR_DEVICE;
					CMD_RESULT(sp->cmd) = DID_NO_CONNECT << 16;
					return (TRUE);
				}
				if ((cp->sense_buffer[12] == 0xa &&
					cp->sense_buffer[13] == 0x9)) {
					/* failback lun */
				}
			} 
			/* retry I/O */
 	     		if (fcport->flags & FC_DSXXX_DEVICE) {
				/* lun config changed */
				if (cp->sense_buffer[12] == 0x2a &&
					cp->sense_buffer[13] == 0x6) {
					CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
					return (TRUE);
				}
			}
			break;

		case HARDWARE_ERROR:
			if (fcport->flags & (FC_DFXXX_DEVICE)) {
				sp->err_id = SRB_ERR_DEVICE;
				CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
				return (TRUE);
			}
			break;

		case ABORTED_COMMAND:
			if (fcport->flags & (FC_DFXXX_DEVICE)) {
				sp->err_id = SRB_ERR_DEVICE;
				CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
				return (TRUE);
			}
			break;

	} /* end of switch */
	return (retry);
}

/*
 * qla2x00_fo_check
 *	This function is called from the done routine to see if
 *  the SRB requires a failover.
 *
 *	This function examines the available os returned status and
 *  if meets condition, the command(srb) is placed ont the failover
 *  queue for processing.
 *
 * Input:
 *	sp  = Pointer to the SCSI Request Block
 *
 * Output:
 *      sp->flags SRB_RETRY bit id command is to
 *      be retried otherwise bit is reset.
 *
 * Returns:
 *      None.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
BOOL
qla2x00_fo_check(scsi_qla_host_t *ha, srb_t *sp)
{
	BOOL		retry = FALSE;
	int host_status;
#if DEBUG_QLA2100
	STATIC char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* we failover on selction timeouts and some device check conditions */
	if (sp->err_id == SRB_ERR_RETRY) {
		sp->cmd->result = DID_BUS_BUSY << 16;
		// spin_lock_irqsave(&ha->hardware_lock, flags);
		sp->fclun->io_cnt++;
		sp->fclun->s_time += HZ;
		// spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

        qla2x00_fo_check_device(ha, sp);
	host_status = CMD_RESULT(sp->cmd) >>16;
	if( host_status == DID_NO_CONNECT) {
			
		if( qla2x00_fo_count_retries(ha,sp) ) {
			/* Force a retry  on this request, it will
			 * cause the LINUX timer to get reset, while we
			 * we are processing the failover.
			 */
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
			retry = TRUE;
		}
		DEBUG(printk("qla2x00_fo_check: pid= %ld sp %p "
				"retry count=%d, retry flag = %d, "
				"host status (%s)\n\r",
				sp->cmd->serial_number,
				sp, sp->fo_retry_cnt,
				retry, reason[host_status]);)
		DEBUG(printk("qla2x00_fo_check: pid= %ld sp %p "
				"retry count=%d, retry flag = %d, "
				"host status=%d\n\r",
				sp->cmd->serial_number,
				sp, sp->fo_retry_cnt,
				retry, host_status);)
	}

	/* Clear out any FO retry counts on good completions. */
	if (host_status == DID_OK) {
		set_bit(LUN_MPIO_RESET_CNTS, &sp->lun_queue->q_flag);
	}


	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry;
}

/*
 * qla2x00_fo_path_change
 *	This function is called from configuration mgr to notify
 *	of a path change.
 *
 * Input:
 *      type    = Failover notify type, FO_NOTIFY_LUN_RESET or FO_NOTIFY_LOGOUT
 *      newlunp = Pointer to the fc_lun struct for current path.
 *      oldlunp = Pointer to fc_lun struct for previous path.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
uint32_t
qla2x00_fo_path_change(uint32_t type, fc_lun_t *newlunp, fc_lun_t *oldlunp)
{
	uint32_t	ret = QLA2X00_SUCCESS;

	newlunp->max_path_retries = 0;
	return ret;
}

/*
 * qla2x00_fo_get_params
 *	Process an ioctl request to get system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_get_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	pp->MaxPathsPerDevice = qla_fo_params.MaxPathsPerDevice;
	pp->MaxRetriesPerPath = qla_fo_params.MaxRetriesPerPath;
	pp->MaxRetriesPerIo = qla_fo_params.MaxRetriesPerIo;
	pp->Flags = qla_fo_params.Flags;
	pp->FailoverNotifyType = qla_fo_params.FailoverNotifyType;
	pp->FailoverNotifyCdbLength = qla_fo_params.FailoverNotifyCdbLength;
	memset(pp->FailoverNotifyCdb, 0, sizeof(pp->FailoverNotifyCdb));
	memcpy(pp->FailoverNotifyCdb,
	    &qla_fo_params.FailoverNotifyCdb[0], sizeof(pp->FailoverNotifyCdb));

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}

/*
 * qla2x00_fo_set_params
 *	Process an ioctl request to set system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla2x00_fo_set_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	/* Check values for defined MIN and MAX */
	if ((pp->MaxPathsPerDevice > SDM_DEF_MAX_PATHS_PER_DEVICE) ||
	    (pp->MaxRetriesPerPath < FO_MAX_RETRIES_PER_PATH_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_PATH_MAX) ||
	    (pp->MaxRetriesPerIo < FO_MAX_RETRIES_PER_IO_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_IO_MAX)) {
		DEBUG2_9_10(printk("%s: got invalid params.\n", __func__);)
		return EXT_STATUS_INVALID_PARAM;
	}

	/* Update the global structure. */
	qla_fo_params.MaxPathsPerDevice = pp->MaxPathsPerDevice;
	qla_fo_params.MaxRetriesPerPath = pp->MaxRetriesPerPath;
	qla_fo_params.MaxRetriesPerIo = pp->MaxRetriesPerIo;
	qla_fo_params.Flags = pp->Flags;
	qla_fo_params.FailoverNotifyType = pp->FailoverNotifyType;
	qla_fo_params.FailoverNotifyCdbLength = pp->FailoverNotifyCdbLength;
	if (pp->FailoverNotifyType & FO_NOTIFY_TYPE_CDB) {
		if (pp->FailoverNotifyCdbLength >
		    sizeof(qla_fo_params.FailoverNotifyCdb)) {
			DEBUG2_9_10(printk("%s: got invalid cdb length.\n",
			    __func__);)
			return EXT_STATUS_INVALID_PARAM;
		}

		memcpy(qla_fo_params.FailoverNotifyCdb,
		    pp->FailoverNotifyCdb,
		    sizeof(qla_fo_params.FailoverNotifyCdb));
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}

#if 0
static mp_tport_grp_t *
qla2x00_find_tpg_by_lu_path(mp_lun_t *mplun, lu_path_t *lu_path)
{
	struct list_head *list, *temp;
	mp_tport_grp_t *tport_grp;
	mp_tport_grp_t *tpg = NULL;

	list_for_each_safe(list, temp, &mplun->tport_grps_list) {
		tport_grp = list_entry(list, mp_tport_grp_t, list);
		if (tport_grp->tpg_id[0] == lu_path->tpg_id[0]  &&
		    tport_grp->tpg_id[1] == lu_path->tpg_id[1]  ) {
			tpg = tport_grp;
			break;
		} 
	}
	return tpg;
}
#endif

static lu_path_t *
qla2x00_find_lu_path_by_fclun(mp_lun_t *mplun, fc_lun_t *fclun)
{
	struct list_head *list, *temp;
	lu_path_t  *tmp_path;
	lu_path_t	*lu_path = NULL;

	list_for_each_safe(list, temp, &mplun->lu_paths){
		tmp_path = list_entry(list, lu_path_t, list);
		if (tmp_path->fclun == fclun ) {
			lu_path = tmp_path;
			break;
		}
	}
	return lu_path;
}

static int 
qla2x00_update_tpg_states(fc_lun_t *old_lp, fc_lun_t *new_lp)
{
 	mp_tport_grp_t *new_tpg = NULL;
 	mp_tport_grp_t *old_tpg = NULL;
	mp_lun_t *mplun = (mp_lun_t *)new_lp->mplun; 
	lu_path_t	*new_lu_path;
	lu_path_t	*old_lu_path;
 	uint8_t		passive_state = 0;
 	struct list_head *list, *temp;
 	mp_tport_grp_t *tport_grp;

	new_lu_path = qla2x00_find_lu_path_by_fclun(mplun,new_lp);
	old_lu_path = qla2x00_find_lu_path_by_fclun(mplun,old_lp);
	if( new_lu_path == NULL || old_lu_path == NULL) {
		return 1;
	}
	if( new_lu_path ==  old_lu_path ) {
		DEBUG2(printk("%s Ignoring new path_id =%d,"
			" old path_id =%d\n",__func__,
			new_lu_path->path_id, old_lu_path->path_id);)
		 return 1;
	}
 
 	/* Always change */
 	list_for_each_safe(list, temp, &mplun->tport_grps_list) {
 		tport_grp = list_entry(list, mp_tport_grp_t, list);
 		if (tport_grp->asym_acc_state != TPG_ACT_OPT) {
 			passive_state = tport_grp->asym_acc_state;
 			new_tpg = tport_grp;
 		} else  {
 			old_tpg = tport_grp;
 		}
 	}
 
 	if( new_tpg == NULL || old_tpg == NULL) {
 		return 1;
 	}
 
 	old_tpg->asym_acc_state = passive_state;	
  	new_tpg->asym_acc_state = TPG_ACT_OPT;	
 
 	old_lu_path->asym_acc_state = old_tpg->asym_acc_state;
 	new_lu_path->asym_acc_state = new_tpg->asym_acc_state;
 
 	DEBUG2(printk(
 	"%s TPG STATES: lun %d  new_tpg[%d]=%d new tpg %p, old_tpg[%d]=%d old tpg %p\n",
 		__func__, mplun->number, new_tpg->tpg_id[1], 
 		new_tpg->asym_acc_state,new_tpg,old_tpg->tpg_id[1],
 		old_tpg->asym_acc_state,old_tpg);)
 	DEBUG2(printk(
 	"%s lun %d  new_lu_path[%d]=%d (new state), old_lun_path[%d]=%d (old state)\n",
 		__func__, mplun->number,  new_lu_path->path_id,
 		new_lu_path->asym_acc_state, old_lu_path->path_id,
 		old_lu_path->asym_acc_state);)

	return 0;
}

static int 
qla2x00_issue_set_tpg_cdb (fc_lun_t *new_lp)
{
	int		rval = QLA2X00_SUCCESS;
	int		retry;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint16_t	tpg_count;
	//uint16_t	tpg_id;
	uint16_t	lun = 0;
	uint8_t		passive_state = 0;
	dma_addr_t	stpg_dma;
	scsi_qla_host_t *ha;
	fc_port_t	*fcport;
	mp_lun_t *mplun;
	set_tport_grp_rsp_t *stpg;
	struct list_head *list, *temp;
	mp_tport_grp_t *tport_grp;
	uint8_t	index = 0;
	uint16_t        *cstatus, *sstatus;
        uint8_t         *sense_data;


	ENTER(__func__);

	DEBUG2(printk("%s  entered lp=%p\n",__func__,new_lp);)
	fcport = new_lp->fcport;
	ha = fcport->ha;
	if (atomic_read(&fcport->state) == FC_DEVICE_DEAD) {
		DEBUG2(printk("scsi(%ld) %s leaving: Port 0x%02x is marked "
			"DEAD\n", ha->host_no,__func__,fcport->loop_id);)
		return (QLA2X00_FUNCTION_FAILED);
	}

	lun = new_lp->lun;
	mplun = new_lp->mplun;	
	if (mplun ==  NULL) {
		DEBUG(printk("%s mplun does not exist for fclun=%p\n",
				__func__,new_lp);)
		return (QLA2X00_FUNCTION_FAILED);
	}

	/* check for ALUA support */
	if (new_lp->asymm_support == TGT_PORT_GRP_UNSUPPORTED || 
		new_lp->asymm_support == SET_TGT_PORT_GRP_UNSUPPORTED) {
		printk("%s(%ld): lun=%d does not support ALUA\n",__func__,ha->instance,lun);
		return rval;
	}

	stpg = pci_alloc_consistent(ha->pdev,
				sizeof(set_tport_grp_rsp_t), &stpg_dma);
	if (stpg == NULL) {
		printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - TPG\n",
				ha->host_no);
		ha->mem_err++;
		return (QLA2X00_FUNCTION_FAILED);
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		cstatus = &stpg->p.rsp24.comp_status;
		sstatus = &stpg->p.rsp24.scsi_status;
		sense_data = stpg->p.rsp24.data;
	} else {
		cstatus = &stpg->p.rsp.comp_status;
		sstatus = &stpg->p.rsp.scsi_status; 
		sense_data = stpg->p.rsp.req_sense_data;
	}


	retry = 5;
	do {
		memset(stpg, 0, sizeof(set_tport_grp_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			stpg->p.cmd24.entry_type = COMMAND_TYPE_7;
			stpg->p.cmd24.entry_count = 1;
			stpg->p.cmd24.nport_handle = fcport->loop_id;
			stpg->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
			stpg->p.cmd24.port_id[1] = fcport->d_id.b.area;
			stpg->p.cmd24.port_id[2] = fcport->d_id.b.domain;
			stpg->p.cmd24.lun[1] = LSB(lun);
			stpg->p.cmd24.lun[2] = MSB(lun);
			host_to_fcp_swap(stpg->p.cmd24.lun,
			    sizeof(stpg->p.cmd24.lun));
			stpg->p.cmd24.task_mgmt_flags =
			    __constant_cpu_to_le16(TMF_WRITE_DATA);
			stpg->p.cmd24.task = TSK_SIMPLE;
			stpg->p.cmd24.fcp_cdb[0] = SCSIOP_MAINTENANCE_OUT;
			stpg->p.cmd24.fcp_cdb[1] = SCSISA_TARGET_PORT_GROUPS;
			stpg->p.cmd24.fcp_cdb[8] = 
				((sizeof(set_tport_grp_data_t) >> 8) & 0xff); 
			stpg->p.cmd24.fcp_cdb[9] = 
					(sizeof(set_tport_grp_data_t) & 0xff);

			host_to_fcp_swap(stpg->p.cmd24.fcp_cdb,
			    sizeof(stpg->p.cmd24.fcp_cdb));

			/* Right now we support only two tgt port groups. For
			 * failover to occur the state of the two controller must be
			 * opposite to each other and different from current state */
			tpg_count = 0; 
			list_for_each_safe(list, temp, &mplun->tport_grps_list) {
				tport_grp = list_entry(list, mp_tport_grp_t, list);
				if (tport_grp->asym_acc_state != TPG_ACT_OPT) {
					passive_state = tport_grp->asym_acc_state;
					stpg->list.descriptor[tpg_count].
						    asym_acc_state = TPG_ACT_OPT;	
				} else  {
					index = tpg_count;
				}
				memcpy(&stpg->list.descriptor[tpg_count].
					tgt_port_grp[0], &tport_grp->tpg_id[0],
					 sizeof(tport_grp->tpg_id));
				DEBUG4(printk("%s lun=%d tpg_id=%d old_tpg_state=%d \n",__func__, lun, 
					tport_grp->tpg_id[1], tport_grp->asym_acc_state);) 
				tpg_count++; 
			}

			/* setting the active controller to passive state */	
			stpg->list.descriptor[index].asym_acc_state = passive_state;	
			stpg->p.cmd24.dseg_count = __constant_cpu_to_le16(1);
			stpg->p.cmd24.timeout = __constant_cpu_to_le16(10);
			stpg->p.cmd24.byte_count =
			__constant_cpu_to_le32(sizeof(set_tport_grp_data_t));
			stpg->p.cmd24.dseg_0_address[0] = cpu_to_le32(
			    LSD(stpg_dma + sizeof(struct sts_entry_24xx)));
			stpg->p.cmd24.dseg_0_address[1] = cpu_to_le32(
			    MSD(stpg_dma + sizeof(struct sts_entry_24xx)));
			stpg->p.cmd24.dseg_0_len =
			__constant_cpu_to_le32(sizeof(set_tport_grp_data_t));
		} else {
			stpg->p.cmd.entry_type = COMMAND_A64_TYPE;
			stpg->p.cmd.entry_count = 1;
			stpg->p.cmd.lun = cpu_to_le16(lun);
	#if defined(EXTENDED_IDS)
			stpg->p.cmd.target = cpu_to_le16(fcport->loop_id);
	#else
			stpg->p.cmd.target = (uint8_t)fcport->loop_id;
	#endif
			stpg->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_WRITE | CF_SIMPLE_TAG);
			stpg->p.cmd.scsi_cdb[0] = SCSIOP_MAINTENANCE_OUT;
			stpg->p.cmd.scsi_cdb[1] = SCSISA_TARGET_PORT_GROUPS;
			stpg->p.cmd.scsi_cdb[8] = ((sizeof(set_tport_grp_data_t) >> 8) & 0xff); 
			stpg->p.cmd.scsi_cdb[9] = (sizeof(set_tport_grp_data_t) & 0xff);

			/* Right now we support only two tgt port groups. For
			 * failover to occur the state of the two controller must be
			 * opposite to each other and different from current state */
			tpg_count = 0; 
			list_for_each_safe(list, temp, &mplun->tport_grps_list) {
				tport_grp = list_entry(list, mp_tport_grp_t, list);
				if (tport_grp->asym_acc_state != TPG_ACT_OPT) {
					passive_state = tport_grp->asym_acc_state;
					stpg->list.descriptor[tpg_count].
						    asym_acc_state = TPG_ACT_OPT;	
				} else  {
					index = tpg_count;
				}
				memcpy(&stpg->list.descriptor[tpg_count].
					tgt_port_grp[0], &tport_grp->tpg_id[0],
					 sizeof(tport_grp->tpg_id));
				DEBUG4(printk("%s lun=%d tpg_id=%d old_tpg_state=%d \n",__func__, lun, 
					tport_grp->tpg_id[1], tport_grp->asym_acc_state);) 
				tpg_count++; 
			}

			/* setting the active controller to passive state */	
			stpg->list.descriptor[index].asym_acc_state = passive_state;	

			stpg->p.cmd.dseg_count = __constant_cpu_to_le16(1);
			stpg->p.cmd.timeout = __constant_cpu_to_le16(10);
			stpg->p.cmd.byte_count =
				__constant_cpu_to_le32(sizeof(set_tport_grp_data_t));
			stpg->p.cmd.dseg_0_address[0] = cpu_to_le32(
					LSD(stpg_dma + sizeof(sts_entry_t)));
			stpg->p.cmd.dseg_0_address[1] = cpu_to_le32(
					MSD(stpg_dma + sizeof(sts_entry_t)));
			stpg->p.cmd.dseg_0_length =
				__constant_cpu_to_le32(sizeof(set_tport_grp_data_t));

		}
		#if  1
		for (tpg_count = 0; tpg_count < TGT_PORT_GRP_COUNT; 
			tpg_count++) {
			printk("%s lun=%d tpg cnt=%d tpg_id[0]=%d tpg_id[1]=%d new_tpg_state=%d\n",__func__, 
				lun, tpg_count,stpg->list.descriptor[tpg_count].tgt_port_grp[0],
				stpg->list.descriptor[tpg_count].tgt_port_grp[1],
				stpg->list.descriptor[tpg_count].asym_acc_state);
		}
		#endif	

		rval = qla2x00_issue_iocb(ha, stpg, stpg_dma,
				sizeof(set_tport_grp_rsp_t));

		comp_status = le16_to_cpup(cstatus);
		scsi_status = le16_to_cpup(sstatus);

		/* Port Logged Out, so don't retry */
		if (comp_status == CS_PORT_LOGGED_OUT ||
		    comp_status == CS_PORT_CONFIG_CHG ||
		    comp_status == CS_PORT_BUSY ||
		    comp_status == CS_INCOMPLETE ||
		    comp_status == CS_PORT_UNAVAILABLE)
				break;


		if (scsi_status & SS_CHECK_CONDITION) {
			DEBUG2(printk("%s: check status bytes = 0x%02x 0x%02x 0x%02x\n", __func__,
				sense_data[2], sense_data[12], sense_data[13]);)

			/* switched status */
 	     		if( (fcport->flags & FC_DSXXX_DEVICE) &&
			    sense_data[2] == 0x6 && sense_data[12] == 0x29 &&
			    sense_data[13] == 0x1) {
				scsi_status = 0; /* make OK */
				break;
			}
			/* Already switched status */
 	     		if( (fcport->flags & FC_DSXXX_DEVICE) &&
			    sense_data[2] == 0x5 && sense_data[12] == 0x26 &&
			    sense_data[13] == 0x0) {
				scsi_status = 0; /* make OK */
				break;
			}

			if (sense_data[2] == NOT_READY &&
			    sense_data[12] == 0x4 && sense_data[13] == 0xa) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(3 * HZ);
			}
		}

		/* printk(KERN_INFO 
			"qla_fo(%ld): Sending Set target port group - retry=%d"
				" comp status 0x%x, "
				"scsi status 0x%x, rval=%d\n",
				ha->host_no,
				retry,
				comp_status,
				scsi_status, 
				rval); */

	} while ((rval != QLA2X00_SUCCESS || comp_status != CS_COMPLETE ||
				(scsi_status & SS_CHECK_CONDITION)) && --retry);

	if (rval == QLA2X00_SUCCESS && retry &&
		(!((scsi_status & SS_CHECK_CONDITION) &&
		   (sense_data[2] == NOT_READY )) &&
		 comp_status == CS_COMPLETE)) {
		DEBUG2(printk("%s Set tgt port group - SUCCEDED, lun (%d) cs=0x%x ss=0x%x, rval=%d\n",
					__func__, lun, comp_status, scsi_status, rval);)
	} else {
		rval = QLA2X00_FUNCTION_FAILED;
		DEBUG2(printk("%s Set tgt port group - FAILED, lun (%d) cs=0x%x ss=0x%x, rval=%d\n",
					__func__, lun, comp_status, scsi_status, rval);)
	}

	pci_free_consistent(ha->pdev, sizeof(set_tport_grp_rsp_t),
			stpg, stpg_dma); 

	/* update state info */
	// qla2x00_get_target_ports(fcport, new_lp, 0);
	DEBUG(printk(KERN_INFO "%s  leaving\n",__func__);)
	LEAVE(__func__);

	return rval;
}


/*
 * qla2x00_fo_init_params
 *	Gets driver configuration file failover properties to initalize
 *	the global failover parameters structure.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_fo_init_params(scsi_qla_host_t *ha)
{
	DEBUG3(printk("%s: entered.\n", __func__);)

	/* For parameters that are not completely implemented yet, */

	memset(&qla_fo_params, 0, sizeof(qla_fo_params));

	if(MaxPathsPerDevice) {
		qla_fo_params.MaxPathsPerDevice = MaxPathsPerDevice;
	} else
		qla_fo_params.MaxPathsPerDevice =FO_MAX_PATHS_PER_DEVICE_DEF ;
	if(MaxRetriesPerPath) {
		qla_fo_params.MaxRetriesPerPath = MaxRetriesPerPath;
	} else
		qla_fo_params.MaxRetriesPerPath =FO_MAX_RETRIES_PER_PATH_DEF;
	if(MaxRetriesPerIo) {
		qla_fo_params.MaxRetriesPerIo =MaxRetriesPerIo;
	} else
		qla_fo_params.MaxRetriesPerIo =FO_MAX_RETRIES_PER_IO_DEF;

	qla_fo_params.Flags =  0;
	qla_fo_params.FailoverNotifyType = FO_NOTIFY_TYPE_NONE;
	
	/* Set it to whatever user specified on the cmdline */
	if(qlFailoverNotifyType != FO_NOTIFY_TYPE_NONE)
		qla_fo_params.FailoverNotifyType = qlFailoverNotifyType;
	

	DEBUG3(printk("%s: exiting.\n", __func__);)

}

int
qla2x00_spinup(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun) 
{
	inq_cmd_rsp_t	*pkt;
	int		rval = QLA2X00_SUCCESS;
	int		count, retry;
	dma_addr_t	phys_address = 0;
	uint16_t	comp_status = CS_COMPLETE;
	uint16_t	scsi_status = 0;
        uint16_t        *cstatus, *sstatus;
        uint8_t         *sense_data;

	ENTER(__func__);

	pkt = pci_alloc_consistent(ha->pdev,
				sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
			"scsi(%ld): Memory Allocation failed - INQ\n",
			ha->host_no);
		return( QLA2X00_FAILED);
	}
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                cstatus = &pkt->p.rsp24.comp_status;
                sstatus = &pkt->p.rsp24.scsi_status;
                sense_data = pkt->p.rsp24.data;
        } else {
                cstatus = &pkt->p.rsp.comp_status;
                sstatus = &pkt->p.rsp.scsi_status;
                sense_data = pkt->p.rsp.req_sense_data;
        }

	count = 5; 
	retry = 5;
	if (atomic_read(&fcport->state) != FC_ONLINE){
		DEBUG2(printk("scsi(%ld) %s leaving: Port 0x%02x is not ONLINE\n",
			ha->host_no,__func__,fcport->loop_id);)
		rval =  QLA2X00_FAILED;
	}
	else do {
		/* issue spinup */
		memset(pkt, 0, sizeof(inq_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                        pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
                        pkt->p.cmd24.entry_count = 1;
                        pkt->p.cmd24.nport_handle = cpu_to_le16(fcport->loop_id);
                        pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
                        pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
                        pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
                        pkt->p.cmd24.lun[1] = LSB(lun);
                        pkt->p.cmd24.lun[2] = MSB(lun);
                        host_to_fcp_swap(pkt->p.cmd24.lun,
                            sizeof(pkt->p.cmd24.lun));
                        pkt->p.cmd24.task = TSK_SIMPLE;
                        pkt->p.cmd24.fcp_cdb[0] = START_STOP;
			pkt->p.cmd24.fcp_cdb[4] = 1;	/* start spin cycle */
                        host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
                            sizeof(pkt->p.cmd24.fcp_cdb));
                        pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(0);
                        pkt->p.cmd24.timeout = __constant_cpu_to_le16(20);
                } else {
			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = cpu_to_le16(lun);
#if defined(EXTENDED_IDS)
			pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
#else
			pkt->p.cmd.target = (uint8_t)fcport->loop_id;
#endif
			/* no direction for this command */
			pkt->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_SIMPLE_TAG);
			pkt->p.cmd.scsi_cdb[0] = START_STOP;
			pkt->p.cmd.scsi_cdb[4] = 1;	/* start spin cycle */
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(0);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(20);
			pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);
		}

		rval = qla2x00_issue_iocb(ha, pkt,
				phys_address, sizeof(inq_cmd_rsp_t));

		comp_status = le16_to_cpu(*cstatus);
		scsi_status = le16_to_cpu(*sstatus);

 		/* Port Logged Out, so don't retry */
		if( 	comp_status == CS_PORT_LOGGED_OUT  ||
			comp_status == CS_PORT_CONFIG_CHG ||
			comp_status == CS_PORT_BUSY ||
			comp_status == CS_INCOMPLETE ||
			comp_status == CS_PORT_UNAVAILABLE ) {
			break;
		}

		if ( (scsi_status & SS_CHECK_CONDITION) ) {
				DEBUG2(printk("%s(%ld): SS_CHECK_CONDITION "
						"Sense Data "
						"%02x %02x %02x %02x "
						"%02x %02x %02x %02x\n",
						__func__,
						ha->host_no, sense_data[0],
						sense_data[1], sense_data[2],
						sense_data[3], sense_data[4],
						sense_data[5], sense_data[6],
						sense_data[7]);)
				if (sense_data[2] == NOT_READY  &&
				   (sense_data[12] == 4 ) && (sense_data[13] == 3 ) ) {

 					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule_timeout(HZ);
					printk(".");
					count--;
				} else
					retry--;
		}

		printk(KERN_INFO 
			"qla_fo(%ld): Sending Start - count %d, retry=%d"
				" comp status 0x%x, "
				"scsi status 0x%x, rval=%d\n",
				ha->host_no,
				count,
				retry,
				comp_status,
				scsi_status, 
				rval);

		if ( (rval != QLA2X00_SUCCESS) ||
		     (comp_status != CS_COMPLETE)  )
			retry--;

	} while ( count && retry  &&
		 (rval != QLA2X00_SUCCESS ||
		  comp_status != CS_COMPLETE ||
		  (scsi_status & SS_CHECK_CONDITION) ) );


	if (rval != QLA2X00_SUCCESS ||
		comp_status != CS_COMPLETE ||
		(scsi_status & SS_CHECK_CONDITION)) {

		DEBUG(printk("qla_fo(%ld): Failed spinup - "
				"comp status 0x%x, "
				"scsi status 0x%x. loop_id=%d\n",
				ha->host_no,
				comp_status,
				scsi_status, 
				fcport->loop_id);)
				rval =  QLA2X00_FAILED;
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t),
			pkt, phys_address);


	LEAVE(__func__);

	return( rval );

}

uint32_t
qla2x00_wait_for_tpg_avail(fc_lun_t *new_lp)
{
	int seconds = 60;	/* seconds * 2 */
	mp_tport_grp_t *tport_grp;
	mp_lun_t *mplun;
	int tpg_count;
	struct list_head *list, *temp;
	int	rval = QLA2X00_FAILED;

	DEBUG2(printk("%s: entered.\n", __func__);)
	printk(KERN_INFO "%s: entered.\n", __func__);
	mplun = new_lp->mplun;	
	do  {
		qla2x00_get_target_ports(new_lp->fcport, new_lp, 0);
		tpg_count = 0; 
		list_for_each_safe(list, temp, &mplun->tport_grps_list) {
			tport_grp = list_entry(list, mp_tport_grp_t, list);
			if (tport_grp->asym_acc_state == TPG_UNAVAIL)
				tpg_count++; 
		}
		if (tpg_count == 0 ) {
			rval = QLA2X00_SUCCESS;
			break;
		}
 		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ*2);
	} while (--seconds); 


	printk(KERN_INFO "%s: leaving tpg_count=%d seconds=%d.\n", __func__, tpg_count, seconds);

	return rval;
}


uint32_t
qla2x00_wait_for_tpg_ready(fc_lun_t *new_lp)
{
        int seconds = 60;
        int rval = 0, completed =0;
        uint8_t  wait_for_transition;

        DEBUG2(printk("%s: entered.\n", __func__);)
        wait_for_transition = 1;
        do  {
                rval = qla2x00_test_active_lun(new_lp->fcport,
                        new_lp, &wait_for_transition);
                if(rval == 1 || wait_for_transition == 0) {
                        completed++;
                        break;
                }
                set_current_state(TASK_UNINTERRUPTIBLE);
                schedule_timeout(HZ * 2);
        } while (--seconds);

        if (completed)
                rval = QLA2X00_SUCCESS;
        else
                rval = QLA2X00_FUNCTION_FAILED;
        DEBUG2(printk("%s: leaving rval=%d seconds=%d.\n", __func__,
                        rval, seconds);)

        return rval;
}

	

/*
 * qla2x00_send_fo_notification
 *      Sends failover notification if needed.  Change the fc_lun pointer
 *      in the old path lun queue.
 *
 * Input:
 *      old_lp = Pointer to old fc_lun.
 *      new_lp = Pointer to new fc_lun.
 *
 * Returns:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
uint32_t
qla2x00_send_fo_notification(fc_lun_t *old_lp, fc_lun_t *new_lp)
{
	scsi_qla_host_t	*old_ha = old_lp->fcport->ha;
	int		rval = QLA2X00_SUCCESS;
	inq_cmd_rsp_t	*pkt;
	uint16_t	loop_id, lun;
	dma_addr_t	phys_address;
        uint16_t        *cstatus, *sstatus;


	ENTER("qla2x00_send_fo_notification");
	DEBUG3(printk("%s: entered.\n", __func__);)

	if( new_lp->fcport == NULL ){
		DEBUG2(printk("qla2x00_send_fo_notification: No "
			    "new fcport for lun pointer\n");)
		return QLA2X00_FAILED;
	}
	loop_id = new_lp->fcport->loop_id;
	lun = new_lp->lun;

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LUN_RESET) {
		rval = qla2x00_lun_reset(old_ha, new_lp->fcport, lun);
		if (rval == QLA2X00_SUCCESS) {
			DEBUG4(printk("qla2x00_send_fo_notification: LUN "
			    "reset succeded\n");)
		} else {
			DEBUG4(printk("qla2x00_send_fo_notification: LUN "
			    "reset failed\n");)
		}

	}
	if ( (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET) ||
	    (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_CDB) )  {

		rval = qla2x00_fabric_logout(old_ha, loop_id,
		    new_lp->fcport->d_id.b.domain, new_lp->fcport->d_id.b.area,
		    new_lp->fcport->d_id.b.al_pa);
		if (rval == QLA2X00_SUCCESS) {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "logout succeded\n");)
		} else {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "logout failed\n");)
		}

	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_SPINUP ||
		new_lp->fcport->notify_type == FO_NOTIFY_TYPE_SPINUP ) {
		rval = qla2x00_spinup(new_lp->fcport->ha, new_lp->fcport, 
			new_lp->lun); 
	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_TPGROUP_CDB ||
		   old_lp->fcport->notify_type == FO_NOTIFY_TYPE_TPGROUP_CDB) {
		/* send set target port group cdb */	
		rval = qla2x00_wait_for_tpg_avail(new_lp);
		if (rval == QLA2X00_SUCCESS) {
			rval = qla2x00_issue_set_tpg_cdb(new_lp);
			if (rval == QLA2X00_SUCCESS) {
				qla2x00_wait_for_tpg_ready(new_lp);
				DEBUG2(printk("qla2x00_send_fo_failover_notify: "
			    	"set tgt port group succeded\n");)
				qla2x00_update_tpg_states(old_lp, new_lp);
			
			} else {
				DEBUG2(printk("qla2x00_send_fo_failover_notify: "
			    	"set tgt port group failed\n");)
			}
		}
	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB) {
		pkt = pci_alloc_consistent(old_ha->pdev,
		    sizeof(inq_cmd_rsp_t), &phys_address);
		if (pkt == NULL) {
			DEBUG4(printk("qla2x00_send_fo_failover_notify: "
			    "memory allocation failed\n");)

			return(QLA2X00_FUNCTION_FAILED);
		}
		if (check_24xx_or_54xx_device_ids(old_ha) || check_25xx_device_ids(old_ha)) {
			cstatus = &pkt->p.rsp24.comp_status;
			sstatus = &pkt->p.rsp24.scsi_status;
		} else {
			cstatus = &pkt->p.rsp.comp_status;
			sstatus = &pkt->p.rsp.scsi_status;
		}

		memset(pkt,0, sizeof(inq_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(old_ha) || check_25xx_device_ids(old_ha)) {
			pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
			pkt->p.cmd24.entry_count = 1;
			pkt->p.cmd24.nport_handle = loop_id;
			pkt->p.cmd24.port_id[0] = new_lp->fcport->d_id.b.al_pa;
			pkt->p.cmd24.port_id[1] = new_lp->fcport->d_id.b.area;
			pkt->p.cmd24.port_id[2] = new_lp->fcport->d_id.b.domain;
			pkt->p.cmd24.lun[1] = LSB(lun);
			pkt->p.cmd24.lun[2] = MSB(lun);
			host_to_fcp_swap(pkt->p.cmd24.lun,
			    sizeof(pkt->p.cmd24.lun));
			pkt->p.cmd24.task = TSK_SIMPLE;
			memcpy(pkt->p.cmd24.fcp_cdb,
			    qla_fo_params.FailoverNotifyCdb,
			    qla_fo_params.FailoverNotifyCdbLength);
			host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
			    sizeof(pkt->p.cmd24.fcp_cdb));
			pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(1);
			pkt->p.cmd24.timeout = __constant_cpu_to_le16(10);
			pkt->p.cmd24.byte_count = __constant_cpu_to_le32(0);
		} else {

			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = cpu_to_le16(lun);
#if defined(EXTENDED_IDS)
			pkt->p.cmd.target = cpu_to_le16(loop_id);
#else
			pkt->p.cmd.target = (uint8_t)loop_id;
#endif
			/* FIXME: How do you know the direction ???? */
			/* This has same issues as passthur commands - you 
			 * need more than just the CDB.
			 */
			pkt->p.cmd.control_flags =__constant_cpu_to_le16(CF_SIMPLE_TAG);
			memcpy(pkt->p.cmd.scsi_cdb,
			    qla_fo_params.FailoverNotifyCdb,
			    qla_fo_params.FailoverNotifyCdbLength);
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(1);
			pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);
			pkt->p.cmd.dseg_0_address[0] = cpu_to_le32(
			      LSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_address[1] = cpu_to_le32(
			      MSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_length = __constant_cpu_to_le32(0);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
		}

		rval = qla2x00_issue_iocb(old_ha, pkt, phys_address,
		    sizeof (inq_cmd_rsp_t));
		if (rval != QLA2X00_SUCCESS ||
		    le16_to_cpup(cstatus) != CS_COMPLETE ||
		    le16_to_cpup(sstatus) & SS_CHECK_CONDITION ||
		    pkt->inq[0] == 0x7f) {

			DEBUG4(printk("qla2x00_fo_notification: send CDB "
			    "failed: comp_status = %x"
			    "scsi_status = %x inq[0] = %x\n",
			    le16_to_cpup(cstatus),
			    le16_to_cpup(sstatus),
			    pkt->inq[0]);)
		}

		pci_free_consistent(old_ha->pdev,
		    sizeof(inq_cmd_rsp_t), pkt, phys_address);
	}

	DEBUG3(printk("%s: exiting. rval = %d.\n", __func__, rval);)

	return rval;
}


/*
 * qla2100_fo_enabled
 *      Reads and validates the failover enabled property.
 *
 * Input:
 *      ha = adapter state pointer.
 *      instance = HBA number.
 *
 * Returns:
 *      TRUE when failover is authorized else FALSE
 *
 * Context:
 *      Kernel context.
 */
BOOL
qla2x00_fo_enabled(scsi_qla_host_t *ha, int instance)
{
	BOOL enable = FALSE;

	if (ha->flags.failover_enabled)
		enable = TRUE;

	return enable;
}

/*
 * qla2x00_fo_missing_port_summary
 *	Returns values of devices not connected but found in configuration
 *	file in user's dd_entry list.
 *
 * Input:
 *	ha = adapter state pointer.
 *	pdd_entry = pointer to a temporary EXT_DEVICEDATAENTRY struct
 *	pstart_of_entry_list = start of user addr of buffer for dd_entry entries
 *	max_entries = max number of entries allowed by user buffer
 *	pentry_cnt = pointer to total number of entries so far
 *	ret_status = pointer to ioctl status field
 *
 * Returns:
 *	0 = success
 *	others = errno value
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_fo_missing_port_summary(scsi_qla_host_t *ha,
    EXT_DEVICEDATAENTRY *pdd_entry, void *pstart_of_entry_list,
    uint32_t max_entries, uint32_t *pentry_cnt, uint32_t *ret_status)
{
	int		ret = 0;
	uint8_t 	path_id;
	uint8_t		*usr_temp, *kernel_tmp;
	uint16_t	dev_no;
	uint32_t	b;
	uint32_t	current_offset;
	uint32_t	transfer_size;
	mp_device_t	*dp;
	mp_host_t	*host;
	mp_path_list_t	*pathlist;
	mp_path_t	*path;
	portname_list 	*portname_used = NULL;

	DEBUG9(printk("%s(%ld): inst=%ld entered.\n",
	    __func__, ha->host_no, ha->instance);)

	if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
		DEBUG2_9_10(printk("%s(%ld): no HOST for ha inst %ld.\n",
		    __func__, ha->host_no, ha->instance);)
		*ret_status = EXT_STATUS_DEV_NOT_FOUND;
		return (ret);
	}

	/* Assumption: each port name cannot appear in more than one mpdev
	 * structure.
	 */
	for (dev_no = 0; dev_no < MAX_MP_DEVICES && *pentry_cnt < max_entries;
	    dev_no++) {
		dp = host->mp_devs[dev_no];

		if (dp == NULL)
			continue;

		/* Sanity check */
		if (qla2x00_is_wwn_zero(dp->nodename))
			continue;

		if ((pathlist = dp->path_list) == NULL)
			continue;

		path = pathlist->last;
		for (path_id = 0; path_id < pathlist->path_cnt &&
		    *pentry_cnt < max_entries; path_id++, path = path->next) {

			/* Sanity check */
			if (qla2x00_is_wwn_zero(path->portname))
				continue;

			if (path->config == TRUE && path->port == NULL) {
				/* This path was created from config file
				 * but has not been configured.
				 */
				if (path->host != host) {
					/* path on other host. don't report */
					DEBUG10(printk("%s(%ld): path host %p "
					    "not for current host %p.\n",
					    __func__, ha->host_no, path->host,
					    host);)

					continue;
				}

				/* Check whether we've copied info on this
				 * port name before.  If this is a new port
				 * name, save the port name so we won't copy
				 * it again if it's also found on other hosts.
				 */
				if (qla2x00_port_name_in_list(path->portname,
				    portname_used)) {
					DEBUG10(printk("%s(%ld): found previously "
					    "reported portname=%02x%02x%02x"
					    "%02x%02x%02x%02x%02x.\n",
					    __func__, ha->host_no,
					    path->portname[0],
					    path->portname[1],
					    path->portname[2],
					    path->portname[3],
					    path->portname[4],
					    path->portname[5],
					    path->portname[6],
					    path->portname[7]);)
					continue;
				}

				if ((ret = qla2x00_add_to_portname_list(
				    path->portname, &portname_used))) {
					/* mem alloc error? */
					*ret_status = EXT_STATUS_NO_MEMORY;
					break;
				}

				DEBUG10(printk("%s(%ld): returning missing device "
				    "%02x%02x%02x%02x%02x%02x%02x%02x.\n",
				    __func__, ha->host_no,
				    path->portname[0], path->portname[1],
				    path->portname[2], path->portname[3],
				    path->portname[4], path->portname[5],
				    path->portname[6], path->portname[7]);)

				/* This device was not found. Return
				 * as unconfigured.
				 */
				memcpy(pdd_entry->NodeWWN, dp->nodename,
				    WWN_SIZE);
				memcpy(pdd_entry->PortWWN, path->portname,
				    WWN_SIZE);

				for (b = 0; b < 3 ; b++)
					pdd_entry->PortID[b] = 0;

				/* assume fabric dev so api won't translate the portid from loopid */
				pdd_entry->ControlFlags = EXT_DEF_GET_FABRIC_DEVICE;

				pdd_entry->TargetAddress.Bus    = 0;
				pdd_entry->TargetAddress.Target = dp->dev_id;
				pdd_entry->TargetAddress.Lun    = 0;
				pdd_entry->DeviceFlags          = 0;
				pdd_entry->LoopID               = 0;
				pdd_entry->BaseLunNumber        = 0;

				current_offset = *pentry_cnt *
				    sizeof(EXT_DEVICEDATAENTRY);

				transfer_size = sizeof(EXT_DEVICEDATAENTRY);

				/* now copy up this dd_entry to user */
				usr_temp = (uint8_t *)pstart_of_entry_list +
				    current_offset;
				kernel_tmp = (uint8_t *)pdd_entry;
			 	ret = copy_to_user(usr_temp, kernel_tmp,
				    transfer_size);
				if (ret) {
					*ret_status = EXT_STATUS_COPY_ERR;
					DEBUG9_10(printk("%s(%ld): inst=%ld "
					    "ERROR copy rsp list buffer.\n",
					    __func__, ha->host_no,
					    ha->instance);)
					break;
				}
				*pentry_cnt+=1;
			}

		}

		if (ret || *ret_status) {
			break;
		}
	}

	DEBUG9(printk("%s(%ld): ending entry cnt=%d.\n",
	    __func__, ha->host_no, *pentry_cnt);)

	qla2x00_free_portname_list(&portname_used);

	DEBUG9(printk("%s(%ld): inst=%ld exiting. ret=%d.\n",
	    __func__, ha->host_no, ha->instance, ret);)

	return (ret);
}

/*
 * qla2x00_port_name_in_list
 *	Returns whether we found the specified port name in the list given.
 *
 * Input:
 *	wwpn = pointer to ww port name.
 *	list = pointer to a portname_list list.
 *
 * Returns:
 *	TRUE = found portname in list
 *	FALSE = portname not in list
 *
 * Context:
 *	Kernel context.
 */
STATIC BOOL
qla2x00_port_name_in_list(uint8_t *wwpn, portname_list *list)
{
	BOOL 		found_name = FALSE;
	portname_list	*ptmp;

	for (ptmp = list; ptmp; ptmp = ptmp->pnext) {
		if (qla2x00_is_nodename_equal(ptmp->portname, wwpn)) {
		    found_name = TRUE;
		    break;
		}
	}

	return (found_name);
}

/*
 * qla2x00_add_to_portname_list
 *	Allocates a portname_list member and adds it to the list given
 *	with the specified port name.
 *
 * Input:
 *	wwpn = pointer to ww port name.
 *	plist = pointer to a pointer of portname_list list.
 *
 * Returns:
 *	0 = success
 *	others = errno indicating error
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_add_to_portname_list(uint8_t *wwpn, portname_list **plist)
{
	int		ret = 0;
	portname_list	*ptmp;
	portname_list	*plast;

	if ((ptmp = (portname_list *)KMEM_ZALLOC(sizeof(portname_list), 50))) {

		memcpy(ptmp->portname, wwpn, EXT_DEF_WWN_NAME_SIZE);

		if (*plist) {
			/* Add to tail of list */
			for (plast = *plist; plast->pnext; plast=plast->pnext) {
				/* empty */
			}
			plast->pnext = ptmp;
		} else {
			*plist = ptmp;
		}

	} else {
		DEBUG2_9_10(printk("%s: failed to alloc memory of size (%d)\n",
		    __func__, (int)sizeof(FO_LUN_DATA_LIST));)
		ret = -ENOMEM;
	}

	return (ret);
}

/*
 * qla2x00_free_portname_list
 *	Free the list given.
 *
 * Input:
 *	plist = pointer to a pointer of portname_list list to free.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
STATIC void
qla2x00_free_portname_list(portname_list **plist)
{
	portname_list	*ptmp;
	portname_list	*ptmpnext;

	for (ptmp = *plist; ptmp; ptmp = ptmpnext) {
		ptmpnext = ptmp->pnext;
		KMEM_FREE(ptmp, sizeof(portname_list));
	}
	*plist = NULL;
}


/*
 * qla2x00_fo_get_lbtype
 *      Get the lbtype, go thruoug the host->mp_device
 *	match based on the PortName. If match found 
 * 	return the lbtype.
 *
 * Input:
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_get_lbtype(EXT_IOCTL *pext, int mode)
{
	scsi_qla_host_t  *ha		= NULL;
	int              ret 		= 0;
	int		 devid 		= 0;
	mp_host_t        *host 		= NULL;
	mp_device_t	 *dp 		= NULL;
	uint16_t	 idx		= 0;
	int		 respSize	= 0;

	/* Response */
	PFOTGTLBDATALIST pTgtLBDataList	= NULL;

	DEBUG9(printk("%s: entered.\n", __func__);)
	pext->Status = EXT_STATUS_OK;

	/* Check resp size */
	respSize = sizeof(FOTGTLBDATALIST);

	do { /* do 0 for a quick break */

		if ((int)pext->ResponseLen < respSize) {
			pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
			DEBUG9_10(printk("%s: ERROR ResponseLen %d too small."
			    "\n", __func__, pext->ResponseLen);)
			break;
		}

		/* Allocate memory for response */
		pTgtLBDataList = (PFOTGTLBDATALIST)vmalloc(respSize);
		if (pTgtLBDataList == NULL) {
			pext->Status = EXT_STATUS_NO_MEMORY;
			break;

		}

		/* Copy the response from user space */
		ret = copy_from_user(pTgtLBDataList, pext->ResponseAdr,
		    respSize);
		if (ret) {
			DEBUG2_9_10(printk("%s: resp buf copy error. "
			"size=%ld.\n",
			__func__, (ulong)respSize);)

			pext->Status = EXT_STATUS_COPY_ERR;
			ret = (-EFAULT);
			break;
		} 

		/* Reserved field is used to pass in HbaInstance */
		ha = qla2x00_get_hba((int)pTgtLBDataList->Reserved0); 

		if (!ha) {
			DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
			    __func__, (int)pTgtLBDataList->Reserved0);)

			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			break;
		}

		if (ha->flags.failover_enabled) {
			if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
				DEBUG2_9_10(printk("%s: no HOST for ha inst "
				    "%ld.\n", __func__, ha->instance);)
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				break;
			}
		} else {
			/* Failover disable,can not loop hosts */
			DEBUG2_9_10(printk("%s: Non-failover driver %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			break;
		}

	} while (0);

	/* check if error, return */
	if (pext->Status != EXT_STATUS_OK) {
		/* free any memory and return */
		if (pTgtLBDataList) {
			vfree(pTgtLBDataList);	
		}
		
		return (ret);
	}

	/* go through the mp_devices in host and get all the node name */
	for (devid = 0; devid < MAX_MP_DEVICES; devid++) {
		dp = host->mp_devs[devid];
		if (dp == NULL) {
			continue;
		}

		/* go throug the dp to find matching NodeName */
		if (qla2x00_is_wwn_zero(&dp->nodename[0])) {
			continue;
		}
				
		printk(KERN_INFO "%s: %d LB Type is:0x%x\n",
		   __func__, devid, dp->lbtype);

		/* Found a Node Name, get the lbtype */
		DEBUG2_9_10(printk("%s: %ld LB Type is:0x%x\n",
		    __func__, ha->instance, dp->lbtype);)
		if (idx < MAX_LB_ENTRIES ) {
			pTgtLBDataList->Entry[idx].LBPolicy = dp->lbtype;
			memcpy(&pTgtLBDataList->Entry[idx].NodeName[0], 
			    &dp->nodename[0], WWN_SIZE);
		} else {
			DEBUG2_9_10(printk("%s: %ld Array out of bound:\n"
			    , __func__, ha->instance);)
			pext->Status =  EXT_STATUS_DATA_OVERRUN;
			break;
		}
		idx++;
		pTgtLBDataList->EntryCount = idx;
		pext->Status = EXT_STATUS_OK;
	}

	if (pext->Status == EXT_STATUS_OK ) {
		/* copy back the response */
		ret = copy_to_user(pext->ResponseAdr, pTgtLBDataList, respSize);
		if (ret) {
			DEBUG2_9_10(printk("%s(%ld): resp %p copy out err.\n",
			    __func__, ha->host_no, pext->ResponseAdr);)
			pext->Status = EXT_STATUS_COPY_ERR;
			ret = (-EFAULT);
		}
	}

	/* free memory */
	if (pTgtLBDataList) {
		vfree(pTgtLBDataList);	
	}

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)
	return (ret);
}

/*
 * qla2x00_fo_set_lbtype
 *      Set the lbtype, go thruoug the host->mp_device
 *	match based on the NodeName. If match found 
 * 	set the lbtype.
 *
 * Input:
 *
 * Return;
 *      0 on success or errno.
 *
 * Context:
 *      Kernel context.
 */
static int
qla2x00_fo_set_lbtype(EXT_IOCTL *pext, int mode)
{
	scsi_qla_host_t  *ha		= NULL;
	int              ret 		= 0;
	int		 devid 		= 0;
	int		 requestSize	= 0;
	uint16_t	 entryCount	= 0;
	uint16_t	 idx		= 0;
	mp_host_t        *host 		= NULL;
        mp_host_t     	*tmp_host	= NULL;
	mp_device_t	 *dp 		= NULL;

	/* Request */
	PFOTGTLBDATALIST pTgtLBDataList	= NULL;


	DEBUG9(printk("%s: entered.\n", __func__);)

	pext->Status = EXT_STATUS_OK;
	requestSize = sizeof(FOTGTLBDATALIST);

	do { /* do 0 for a quick break */

		if ((int)pext->RequestLen < requestSize) {
			pext->Status = EXT_STATUS_INVALID_PARAM;
			pext->DetailStatus = EXT_DSTATUS_REQUEST_LEN;
			DEBUG10(printk("%s: got invalie req len (%d).\n",
			    __func__, pext->RequestLen);)
			break;
		}

		/* Allocate memory for request */
		pTgtLBDataList = (PFOTGTLBDATALIST)vmalloc(requestSize);
		if (pTgtLBDataList == NULL) {
			pext->Status = EXT_STATUS_NO_MEMORY;
			break;
		}

		/* Copy the request from user space */
		ret = copy_from_user(pTgtLBDataList, (pext->RequestAdr), 
		    requestSize);
		if (ret) {
			DEBUG2_9_10(printk("%s: req buf copy error size=%ld.\n",
			__func__, (ulong)requestSize);)
			pext->Status = EXT_STATUS_COPY_ERR;
			ret = (-EFAULT);
			break;
		} 

		/* Reserved field is used to pass in HbaInstance */
		ha = qla2x00_get_hba((int)pTgtLBDataList->Reserved0); 

		if (!ha) {
			DEBUG2_9_10(printk("%s: no ha matching inst %d.\n",
			    __func__, (int)pTgtLBDataList->Reserved0);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			break;
		}

		if (ha->flags.failover_enabled) {
			if ((host = qla2x00_cfg_find_host(ha)) == NULL) {
				DEBUG2_9_10(printk("%s: no HOST for ha inst "
				    "%ld.\n", __func__, ha->instance);)
				pext->Status = EXT_STATUS_DEV_NOT_FOUND;
				break;
			}
		} else {
			/* Failover disable,can not loop hosts */
			DEBUG2_9_10(printk("%s: Non-failover driver %ld.\n",
			    __func__, ha->instance);)
			pext->Status = EXT_STATUS_DEV_NOT_FOUND;
			break;
		}

	} while (0);
	/* check if error, return */
	if (pext->Status != EXT_STATUS_OK) {
		/* free any memory and return */
		if (pTgtLBDataList) {
			vfree(pTgtLBDataList);	
		}
		return (pext->Status);
	}

	/* Loop for all the targets here */
	entryCount = pTgtLBDataList->EntryCount;
	DEBUG9(printk("%s(): Entry Count = %d\n", __func__, entryCount));
	for (idx = 0; idx < entryCount; idx++) {
		devid = 0;
		/* reset Status */
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		DEBUG9(printk("%s(): Status reset\n", __func__));

		/* go through all the hosts and set the lbtype 
		 * in matching dp->node name
		 */
		for (tmp_host = mp_hosts_base; (tmp_host); 
		    tmp_host = tmp_host->next) {

			/* go through the mp_devices in host and
			 * match the node name 
			 */
		for (devid = 0; devid < MAX_MP_DEVICES; devid++) {
				dp = tmp_host->mp_devs[devid];
			if (dp == NULL) {
				continue;
			}
				/* go throug the dp to find matching NodeName */
				if (qla2x00_is_wwn_zero(&dp->nodename[0])) {
					continue;
				}
				
				if (memcmp(&dp->nodename[0], 
				    &pTgtLBDataList->Entry[idx].NodeName[0], 
				    WWN_SIZE) == 0) {
					/* Found matching Node Name, 
					 * set the lbtype 
					 */

					DEBUG2_9_10(printk("%s: %ld LB Type is:"
					    " 0x%x\n",
					   __func__, ha->instance, dp->lbtype);)

					dp->lbtype = 
					    pTgtLBDataList->Entry[idx].LBPolicy;

					DEBUG2_9_10(printk("%s: %ld LB Type "
					    "after is: 0x%x\n",
					   __func__, ha->instance, dp->lbtype);)
					pext->Status = EXT_STATUS_OK;
					break; /* search for next */
				}
			}
		}
	}

	if (pTgtLBDataList) {
		vfree(pTgtLBDataList);	
	}

	DEBUG9(printk("%s: exiting. ret = %d.\n", __func__, ret);)
	return (ret);
}
