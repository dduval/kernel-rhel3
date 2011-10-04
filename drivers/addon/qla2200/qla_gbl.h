/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/******************************************************************************
* Global include file.
******************************************************************************/


#if !defined(_QLA_GBL_H)
#define	_QLA_GBL_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "exioct.h"

#include "qla_fo.h"

/*
 * Global Data in qla_fo.c source file.
 */
extern SysFoParams_t qla_fo_params;
/*
 * Global Function Prototypes in qla2x00.c source file.
 */
extern int qla2x00_get_prop_xstr(scsi_qla_host_t *, char *, uint8_t *, int);

extern void qla2x00_formatted_print(char *, uint64_t , uint8_t, uint8_t);
extern void qla2x00_formatted_dump_buffer(char *, uint8_t *, uint8_t ,
			uint32_t );
extern inline void *kmem_zalloc( int siz, int code, int id);
extern uint32_t qla2x00_fo_path_change(uint32_t ,
					       fc_lun_t *, fc_lun_t *);
extern scsi_qla_host_t *qla2x00_get_hba(int);
extern int qla24xx_refresh_flash_version(scsi_qla_host_t *, uint8_t *);

/*
 * Global Function Prototypes in qla_fo.c source file.
 */
extern uint32_t qla2x00_send_fo_notification(fc_lun_t *fclun_p, fc_lun_t *olun_p);
extern void qla2x00_fo_init_params(scsi_qla_host_t *ha);
extern BOOL qla2x00_fo_enabled(scsi_qla_host_t *ha, int instance);
extern uint32_t qla2x00_wait_for_tpg_ready(fc_lun_t *);
extern int qla2x00_spinup(scsi_qla_host_t *, fc_port_t *, uint16_t ); 

/*
 * Global Data in qla_cfg.c source file.
 */
extern mp_host_t  *mp_hosts_base;
extern BOOL   mp_config_required;
/*
 * Global Function Prototypes in qla_cfg.c source file.
 */
extern int qla2x00_cfg_init (scsi_qla_host_t *ha);
extern int qla2x00_cfg_path_discovery(scsi_qla_host_t *ha);
extern int qla2x00_cfg_event_notify(scsi_qla_host_t *ha, uint32_t i_type);
extern int qla2x00_cfg_remap(scsi_qla_host_t *);
extern fc_lun_t *qla2x00_cfg_failover(scsi_qla_host_t *ha, fc_lun_t *fp,
					      os_tgt_t *tgt, srb_t *sp);
extern int qla2x00_cfg_get_paths( EXT_IOCTL *, FO_GET_PATHS *, int);
extern int qla2x00_cfg_set_current_path( EXT_IOCTL *,
			FO_SET_CURRENT_PATH *, int);
extern void qla2x00_fo_properties(scsi_qla_host_t *ha);
extern mp_host_t * qla2x00_add_mp_host(uint8_t *);
extern void qla2x00_cfg_mem_free(scsi_qla_host_t *ha);
extern mp_host_t * qla2x00_alloc_host(scsi_qla_host_t *);
extern BOOL qla2x00_fo_check(scsi_qla_host_t *ha, srb_t *sp);
extern mp_path_t *qla2x00_find_path_by_name(mp_host_t *, mp_path_list_t *,
			uint8_t *name);
extern int16_t qla2x00_cfg_lookup_device(unsigned char *response_data);
extern int qla2x00_combine_by_lunid( void *host, uint16_t dev_id, 
	fc_port_t *port, uint16_t pathid); 
extern int qla2x00_export_target( void *host, uint16_t dev_id, 
	fc_port_t *port, uint16_t pathid); 
extern int qla2x00_get_target_ports(fc_port_t *, fc_lun_t *, int);

/*
 * Global Function Prototypes in qla_cfgln.c source file.
 */
extern void qla2x00_cfg_build_path_tree( scsi_qla_host_t *ha);
extern BOOL qla2x00_update_mp_device(mp_host_t *, fc_port_t  *, uint16_t,
    uint16_t);
extern void qla2x00_cfg_display_devices( int flag );

/*
 * Global Function Prototypes in qla_ioctl.c source file.
 */
extern int qla2x00_fo_ioctl(scsi_qla_host_t *, int, EXT_IOCTL *, int);
extern int qla2x00_fo_missing_port_summary(scsi_qla_host_t *,
    EXT_DEVICEDATAENTRY *, void *, uint32_t, uint32_t *, uint32_t *);
extern UINT8
qla2x00_is_fcport_in_config(scsi_qla_host_t *ha, fc_port_t *fcport);
extern UINT8
qla2x00_is_fcport_in_foconfig(scsi_qla_host_t *ha, fc_port_t *fcport);

/*
 * Global Function Prototypes for qla_gs.c functions
 */
extern int
qla2x00_mgmt_svr_login(scsi_qla_host_t *);
extern void
qla2x00_fdmi_srb_tmpmem_free(srb_t *);
extern void
qla2x00_fdmi_register(scsi_qla_host_t *);
extern void
qla2x00_fdmi_register_intr(scsi_qla_host_t *);

#if defined(QLA_CONFIG_COMPAT)
extern int qla2x00_ioctl32(unsigned int, unsigned int, unsigned long,
    struct file *);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* _QLA_GBL_H */
