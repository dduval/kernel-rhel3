/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_crtn.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef _H_LPFC_CRTN
#define _H_LPFC_CRTN

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <scsi.h>

#include "lpfc_sli.h"
#include "lpfc_scsi.h"
#include "lpfc_logmsg.h"

#include "lpfc_compat.h"

int lpfc_proc_info(char *, char **, off_t , int , int , int);

void lpfc_dump_mem(lpfcHBA_t *, LPFC_MBOXQ_t *, uint16_t);
void lpfc_read_nv(lpfcHBA_t *, LPFC_MBOXQ_t *);
int lpfc_read_la(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_clear_la(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_config_link(lpfcHBA_t *, LPFC_MBOXQ_t *);
int lpfc_read_sparam(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_read_config(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_set_slim(lpfcHBA_t *, LPFC_MBOXQ_t *, uint32_t, uint32_t);
void lpfc_config_farp(lpfcHBA_t *, LPFC_MBOXQ_t *);
int lpfc_reg_login(lpfcHBA_t *, uint32_t, uint8_t *, LPFC_MBOXQ_t *, uint32_t);
void lpfc_unreg_login(lpfcHBA_t *, uint32_t, LPFC_MBOXQ_t *);
void lpfc_unreg_did(lpfcHBA_t *, uint32_t, LPFC_MBOXQ_t *);
void lpfc_init_link(lpfcHBA_t *, LPFC_MBOXQ_t *, uint32_t, uint32_t);
uint32_t *lpfc_config_pcb_setup(lpfcHBA_t *);
int lpfc_read_rpi(lpfcHBA_t *, uint32_t, LPFC_MBOXQ_t *, uint32_t);


int lpfc_linkdown(lpfcHBA_t *);
int lpfc_linkup(lpfcHBA_t *);
void lpfc_mbx_cmpl_read_la(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_config_link(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_read_sparam(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_clear_la(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_reg_login(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fabric_reg_login(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_ns_reg_login(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fdmi_reg_login(lpfcHBA_t *, LPFC_MBOXQ_t *);
int lpfc_nlp_bind(lpfcHBA_t *, LPFC_BINDLIST_t *);
int lpfc_nlp_plogi(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_adisc(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_unmapped(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_mapped(struct lpfcHBA *, LPFC_NODELIST_t *, LPFC_BINDLIST_t *);
void lpfc_set_npr_tmo(lpfcHBA_t *, LPFCSCSITARGET_t *, LPFC_NODELIST_t *);
int lpfc_can_npr_tmo(lpfcHBA_t *, LPFCSCSITARGET_t *, LPFC_NODELIST_t *);
void lpfc_set_disctmo(lpfcHBA_t *);
int lpfc_can_disctmo(lpfcHBA_t *);
int lpfc_driver_abort(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_no_rpi(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_new_rpi(lpfcHBA_t *, uint16_t);
void lpfc_dequenode(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_freenode(lpfcHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_remove(lpfcHBA_t *, LPFC_NODELIST_t *);
LPFC_NODELIST_t *lpfc_findnode_did(lpfcHBA_t *, uint32_t, uint32_t);
LPFC_NODELIST_t *lpfc_findnode_scsiid(lpfcHBA_t *, uint32_t);
LPFC_NODELIST_t *lpfc_findnode_wwpn(lpfcHBA_t *, uint32_t, NAME_TYPE *);
LPFC_NODELIST_t *lpfc_findnode_wwnn(lpfcHBA_t *, uint32_t, NAME_TYPE *);
void lpfc_disc_list_loopmap(lpfcHBA_t *);
void lpfc_disc_start(lpfcHBA_t *);
void lpfc_disc_flush_list(lpfcHBA_t *);
void lpfc_disc_timeout(unsigned long);
void lpfc_linkdown_timeout(unsigned long);
void lpfc_nodev_timeout(unsigned long);
LPFCSCSILUN_t *lpfc_find_lun(lpfcHBA_t *, uint32_t, uint64_t, int);
LPFC_SCSI_BUF_t *lpfc_build_scsi_cmd(lpfcHBA_t *, LPFC_NODELIST_t *, uint32_t,
				    uint64_t);
int lpfc_disc_issue_rptlun(lpfcHBA_t *, LPFC_NODELIST_t *);
void lpfc_set_failmask(lpfcHBA_t *, LPFC_NODELIST_t *, uint32_t, uint32_t);

LPFC_NODELIST_t *lpfc_findnode_rpi(lpfcHBA_t * phba, uint16_t rpi);

int lpfc_discq_post_event(lpfcHBA_t *, void *, void *, uint32_t);
void lpfc_tasklet(unsigned long);
void lpfc_flush_disc_evtq(lpfcHBA_t * phba);
int lpfc_disc_state_machine(lpfcHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_disc_nodev(lpfcHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_disc_neverdev(lpfcHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);

uint32_t lpfc_rcv_plogi_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_els_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_els_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_add_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_device_unk_unused_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_rcv_plogi_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prli_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_logo_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_els_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_plogi_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_prli_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_logo_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_adisc_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_plogi_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_rcv_plogi_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_rcv_prli_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_rcv_logo_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_rcv_padisc_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_rcv_prlo_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_cmpl_plogi_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_cmpl_prli_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_cmpl_logo_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_cmpl_adisc_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_reglogin_issue(lpfcHBA_t *,
					   LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_device_rm_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_unk_reglogin_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_rcv_plogi_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_prli_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_padisc_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prlo_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_plogi_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_prli_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_logo_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_adisc_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_rm_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_device_add_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_prli_issue(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_plogi_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_prli_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_padisc_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prlo_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_logo_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_adisc_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_rm_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_device_add_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_prli_compl(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_plogi_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prli_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_logo_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_padisc_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_rcv_prlo_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_logo_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_adisc_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_add_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_device_unk_mapped_node(lpfcHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);

int lpfc_check_sparm(lpfcHBA_t *, LPFC_NODELIST_t *, SERV_PARM *, uint32_t);
int lpfc_geportname(NAME_TYPE *, NAME_TYPE *);
uint32_t lpfc_add_bind(lpfcHBA_t * phba, uint8_t bind_type,
		       void *bind_id, uint32_t scsi_id);
uint32_t lpfc_del_bind(lpfcHBA_t * phba, uint8_t bind_type,
		       void *bind_id, uint32_t scsi_id);

int lpfc_initial_flogi(lpfcHBA_t *);
int lpfc_issue_els_flogi(lpfcHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_els_abort_flogi(lpfcHBA_t *);
int lpfc_issue_els_plogi(lpfcHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_prli(lpfcHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_adisc(lpfcHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_logo(lpfcHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_scr(lpfcHBA_t *, uint32_t, uint8_t);
int lpfc_issue_els_farp(lpfcHBA_t *, uint8_t *, LPFC_FARP_ADDR_TYPE);
int lpfc_issue_els_farpr(lpfcHBA_t *, uint32_t, uint8_t);
LPFC_IOCBQ_t *lpfc_prep_els_iocb(lpfcHBA_t *, uint8_t expectRsp,
				uint16_t, uint8_t, LPFC_NODELIST_t *, uint32_t);
int lpfc_els_free_iocb(lpfcHBA_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_flogi(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_plogi(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_prli(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_adisc(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_logo(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_cmd(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_acc(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_els_logo_acc(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
int lpfc_els_rsp_acc(lpfcHBA_t *, uint32_t, LPFC_IOCBQ_t *,
		     LPFC_NODELIST_t *, LPFC_MBOXQ_t *, uint8_t);
int lpfc_els_rsp_reject(lpfcHBA_t *, uint32_t, LPFC_IOCBQ_t *,
			LPFC_NODELIST_t *);
int lpfc_els_rsp_adisc_acc(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rsp_prli_acc(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_retry(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_els_retry_delay(unsigned long);
void lpfc_els_unsol_event(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *);
int lpfc_els_chk_latt(lpfcHBA_t *, LPFC_IOCBQ_t *);
int lpfc_els_handle_rscn(lpfcHBA_t *);
void lpfc_more_adisc(lpfcHBA_t *);
void lpfc_more_plogi(lpfcHBA_t *);
int lpfc_els_flush_rscn(lpfcHBA_t *);
void lpfc_els_flush_cmd(lpfcHBA_t *);
int lpfc_rscn_payload_check(lpfcHBA_t *, uint32_t);
void lpfc_els_timeout_handler(unsigned long ptr);


void lpfc_ct_unsol_event(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *);
int lpfc_ns_cmd(lpfcHBA_t *, LPFC_NODELIST_t *, int);
int lpfc_ct_cmd(lpfcHBA_t *, DMABUF_t *, DMABUF_t *,
		LPFC_NODELIST_t *, void (*cmpl) (struct lpfcHBA *,
						 LPFC_IOCBQ_t *,
						 LPFC_IOCBQ_t *),
		uint32_t);
int lpfc_free_ct_rsp(lpfcHBA_t *, DMABUF_t *);
int lpfc_ns_rsp(lpfcHBA_t *, DMABUF_t *, uint32_t);
int lpfc_issue_ct_rsp(lpfcHBA_t *, uint32_t, DMABUF_t *, DMABUFEXT_t *);
int lpfc_gen_req(lpfcHBA_t *, DMABUF_t *, DMABUF_t *, DMABUF_t *,
		 void (*cmpl) (struct lpfcHBA *, LPFC_IOCBQ_t *,
			       LPFC_IOCBQ_t *),
		 LPFC_NODELIST_t *, uint32_t, uint32_t, uint32_t);
void lpfc_cmpl_ct_cmd_gid_ft(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rft_id(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rnn_id(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rsnn_nn(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
int lpfc_fdmi_cmd(lpfcHBA_t *, LPFC_NODELIST_t *, int);
void lpfc_cmpl_ct_cmd_fdmi(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
void lpfc_fdmi_tmo(unsigned long);

int lpfc_config_port_prep(lpfcHBA_t *);
int lpfc_config_port_post(lpfcHBA_t *);
int lpfc_hba_down_prep(lpfcHBA_t *);
void lpfc_handle_eratt(lpfcHBA_t *, uint32_t);
void lpfc_handle_latt(lpfcHBA_t *);
void lpfc_hba_init(lpfcHBA_t *);
int lpfc_post_buffer(lpfcHBA_t *, LPFC_SLI_RING_t *, int, int);
void lpfc_cleanup(lpfcHBA_t *, uint32_t);
int lpfc_online(lpfcHBA_t *);
int lpfc_offline(lpfcHBA_t *);
int lpfc_scsi_free(lpfcHBA_t *);
int lpfc_parse_binding_entry(lpfcHBA_t *, uint8_t *, uint8_t *,
			     int, int, int, unsigned int *, int, int *);
void lpfc_decode_firmware_rev(lpfcHBA_t *, char *, int);
uint8_t *lpfc_get_lpfchba_info(lpfcHBA_t *, uint8_t *);
int lpfc_fcp_abort(lpfcHBA_t *, int, int, int);
int lpfc_put_event(lpfcHBA_t *, uint32_t, uint32_t, void *, void *);
int lpfc_hba_put_event(lpfcHBA_t *, uint32_t, uint32_t, uint32_t, uint32_t,
		      uint32_t);
void lpfc_get_hba_model_desc(lpfcHBA_t *, uint8_t *, uint8_t *);
void lpfc_get_hba_sym_node_name(lpfcHBA_t *, uint8_t *);


int lpfc_sli_queue_setup(lpfcHBA_t *);
void lpfc_slim_access(lpfcHBA_t *);


int lpfc_utsname_nodename_check(void);
void lpfc_ip_timeout_handler(unsigned long);
uint32_t fc_get_cfg_param(int, int);

void lpfc_qthrottle_up(unsigned long);
void lpfc_npr_timeout(unsigned long);
void lpfc_scsi_assign_rpi(lpfcHBA_t *, LPFCSCSITARGET_t *, uint16_t);
int lpfc_scsi_hba_reset(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_scsi_issue_inqsn(lpfcHBA_t *, void *, void *);
void lpfc_scsi_issue_inqp0(lpfcHBA_t *, void *, void *);
void lpfc_scsi_timeout_handler(unsigned long);

uint32_t lpfc_intr_prep(struct lpfcHBA *);
void lpfc_handle_eratt(struct lpfcHBA *, uint32_t);
void lpfc_handle_latt(struct lpfcHBA *);
irqreturn_t lpfc_intr_handler(int, void *, struct pt_regs *);

uint32_t lpfc_read_pci(struct lpfcHBA *, int);
void lpfc_setup_slim_access(struct lpfcHBA *);

void lpfc_read_rev(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_config_ring(lpfcHBA_t *, int, LPFC_MBOXQ_t *);
int lpfc_config_port(lpfcHBA_t *, LPFC_MBOXQ_t *);
void lpfc_mbox_put(lpfcHBA_t *, LPFC_MBOXQ_t *);
LPFC_MBOXQ_t *lpfc_mbox_get(lpfcHBA_t *);

int lpfc_mem_alloc(lpfcHBA_t *);
int lpfc_mem_free(lpfcHBA_t *);

int lpfc_sli_hba_setup(lpfcHBA_t *);
int lpfc_sli_hba_down(lpfcHBA_t *);
int lpfc_sli_ring_map(lpfcHBA_t *);
int lpfc_sli_intr(lpfcHBA_t *);
int lpfc_sli_issue_mbox(lpfcHBA_t *, LPFC_MBOXQ_t *, uint32_t);
void lpfc_mbox_abort(lpfcHBA_t *);
int lpfc_sli_issue_iocb(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *,
			uint32_t);
int lpfc_sli_resume_iocb(lpfcHBA_t *, LPFC_SLI_RING_t *);
int lpfc_sli_brdreset(lpfcHBA_t *);
int lpfc_sli_setup(lpfcHBA_t *);
void lpfc_sli_pcimem_bcopy(uint32_t *, uint32_t *, uint32_t);
int lpfc_sli_ringpostbuf_put(lpfcHBA_t *, LPFC_SLI_RING_t *, DMABUF_t *);
DMABUF_t *lpfc_sli_ringpostbuf_get(lpfcHBA_t *, LPFC_SLI_RING_t *, dma_addr_t);
uint32_t lpfc_sli_next_iotag(lpfcHBA_t *, LPFC_SLI_RING_t *);
int lpfc_sli_abort_iocb(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *);
int lpfc_sli_issue_abort_iotag32(lpfcHBA_t *, LPFC_SLI_RING_t *,
				 LPFC_IOCBQ_t *);
int lpfc_sli_abort_iocb_ring(lpfcHBA_t *, LPFC_SLI_RING_t *, uint32_t);
int lpfc_sli_abort_iocb_ctx(lpfcHBA_t *, LPFC_SLI_RING_t *, uint32_t);
int lpfc_sli_abort_iocb_context1(lpfcHBA_t *, LPFC_SLI_RING_t *, void *);
int lpfc_sli_abort_iocb_lun(lpfcHBA_t *, LPFC_SLI_RING_t *, uint16_t, uint64_t);
int lpfc_sli_abort_iocb_tgt(lpfcHBA_t *, LPFC_SLI_RING_t *, uint16_t);
int lpfc_sli_abort_iocb_hba(lpfcHBA_t *, LPFC_SLI_RING_t *);

void lpfc_start_timer(lpfcHBA_t *, unsigned long, struct timer_list *,
	void (*func) (unsigned long), unsigned long, unsigned long); 
void lpfc_stop_timer(struct clk_data *);


void lpfc_sli_wake_iocb_wait(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
int lpfc_sli_issue_iocb_wait(lpfcHBA_t *, LPFC_SLI_RING_t *,
			    LPFC_IOCBQ_t *, uint32_t, LPFC_IOCBQ_t *, uint32_t);
int lpfc_sli_issue_mbox_wait(lpfcHBA_t *, LPFC_MBOXQ_t *, uint32_t);
void lpfc_sli_wake_mbox_wait(lpfcHBA_t *, LPFC_MBOXQ_t *);
int lpfc_sleep(lpfcHBA_t *, void *, long tmo);
void lpfc_wakeup(lpfcHBA_t *, void *);

int lpfc_os_prep_io(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
LPFC_SCSI_BUF_t *lpfc_get_scsi_buf(lpfcHBA_t *);
void lpfc_free_scsi_buf(LPFC_SCSI_BUF_t *);
LPFCSCSILUN_t *lpfc_find_lun_device(LPFC_SCSI_BUF_t *);
void lpfc_map_fcp_cmnd_to_bpl(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_free_scsi_cmd(LPFC_SCSI_BUF_t *);
uint32_t lpfc_os_timeout_transform(lpfcHBA_t *, uint32_t);
void lpfc_os_return_scsi_cmd(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
int lpfc_scsi_cmd_start(LPFC_SCSI_BUF_t *);
int lpfc_scsi_prep_task_mgmt_cmd(lpfcHBA_t *, LPFC_SCSI_BUF_t *, uint8_t);
int lpfc_scsi_cmd_abort(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
int lpfc_scsi_lun_reset(LPFC_SCSI_BUF_t *, lpfcHBA_t *, uint32_t,
		       uint32_t, uint64_t, uint32_t);
int lpfc_scsi_tgt_reset(LPFC_SCSI_BUF_t *, lpfcHBA_t *, uint32_t,
		       uint32_t, uint32_t);

int lpfc_get_inst_by_phba(lpfcHBA_t *);
lpfcHBA_t *lpfc_get_phba_by_inst(int);
int lpfc_check_valid_phba(lpfcHBA_t * phba);

void lpfc_qfull_retry(unsigned long);
void lpfc_scsi_lower_lun_qthrottle(lpfcHBA_t *, LPFC_SCSI_BUF_t *);

void lpfc_sched_init_hba(lpfcHBA_t *, uint16_t);
void lpfc_sched_target_init(LPFCSCSITARGET_t *, uint16_t);
void lpfc_sched_lun_init(LPFCSCSILUN_t *, uint16_t);
int lpfc_sched_submit_command(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_sched_queue_command(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_sched_add_target_to_ring(lpfcHBA_t *, LPFCSCSITARGET_t *);
void lpfc_sched_remove_target_from_ring(lpfcHBA_t *, LPFCSCSITARGET_t *);
void lpfc_sched_add_lun_to_ring(lpfcHBA_t *, LPFCSCSILUN_t *);
void lpfc_sched_remove_lun_from_ring(lpfcHBA_t *, LPFCSCSILUN_t *);
int lpfc_sli_issue_iocb_wait_high_priority(lpfcHBA_t * phba,
					  LPFC_SLI_RING_t * pring,
					  LPFC_IOCBQ_t * piocb, uint32_t flag,
					  LPFC_IOCBQ_t * prspiocbq,
					  uint32_t timeout);
void lpfc_sched_service_high_priority_queue(struct lpfcHBA *hba);
void lpfc_sli_wake_iocb_high_priority(lpfcHBA_t * phba, LPFC_IOCBQ_t * queue1,
				     LPFC_IOCBQ_t * queue2);
void *lpfc_page_alloc(lpfcHBA_t *, int, dma_addr_t *);
void lpfc_page_free(lpfcHBA_t *, void *, dma_addr_t);
void *lpfc_mbuf_alloc(lpfcHBA_t *, int, dma_addr_t *);
void lpfc_mbuf_free(lpfcHBA_t *, void *, dma_addr_t);

LPFC_MBOXQ_t *lpfc_mbox_alloc(lpfcHBA_t *, int);
void lpfc_mbox_free(lpfcHBA_t *, LPFC_MBOXQ_t *);

LPFC_IOCBQ_t *lpfc_iocb_alloc(lpfcHBA_t *, int);
void lpfc_iocb_free(lpfcHBA_t *, LPFC_IOCBQ_t *);

LPFC_NODELIST_t *lpfc_nlp_alloc(lpfcHBA_t *, int);
void lpfc_nlp_free(lpfcHBA_t *, LPFC_NODELIST_t *);

LPFC_BINDLIST_t *lpfc_bind_alloc(lpfcHBA_t *, int);
void lpfc_bind_free(lpfcHBA_t *, LPFC_BINDLIST_t *);


void lpfc_sleep_ms(lpfcHBA_t *, int);
void lpfc_drvr_init_lock(lpfcHBA_t *);
void lpfc_drvr_lock(lpfcHBA_t *, unsigned long *);
void lpfc_drvr_unlock(lpfcHBA_t *, unsigned long *);
void lpfc_hipri_init_lock(lpfcHBA_t *);
void lpfc_hipri_lock(lpfcHBA_t *, unsigned long *);
void lpfc_hipri_unlock(lpfcHBA_t *, unsigned long *);

uint32_t lpfc_read_pci(lpfcHBA_t *, int);

void lpfc_nodev(unsigned long);

void lpfc_iodone(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_scsi_done(lpfcHBA_t *, struct scsi_cmnd *);
int lpfc_scsi_delay_iodone(lpfcHBA_t *, LPFC_SCSI_BUF_t *);
void lpfc_unblock_requests(lpfcHBA_t *);
void lpfc_block_requests(lpfcHBA_t *);
void myprint(char *, void *, void *, void *, void *);

void lpfc_set_pkt_len(struct sk_buff *, uint32_t);
void *lpfc_get_pkt_data(struct sk_buff *);

void lpfc_scsi_cmd_iocb_cmpl(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);

/* Function prototypes. */
int lpfc_revoke(struct scsi_device *pScsiDevice);
int lpfc_queuecommand(struct scsi_cmnd *, void (*done) (struct scsi_cmnd *));
int lpfc_abort_handler(struct scsi_cmnd *);
int lpfc_reset_lun_handler(struct scsi_cmnd *);

#endif				/* _H_LPFC_CRTN */
