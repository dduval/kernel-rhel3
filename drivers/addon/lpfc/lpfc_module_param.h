/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
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
 * $Id: lpfc_module_param.h 1.6.1.9 2004/09/17 18:21:46EDT sf_support Exp  $
 */

#ifndef H_MODULE_PARAM
#define H_MODULE_PARAM

MODULE_PARM(lpfc0_log_verbose, "i");
MODULE_PARM(lpfc0_lun_queue_depth, "i");
MODULE_PARM(lpfc0_tgt_queue_depth, "i");
MODULE_PARM(lpfc0_no_device_delay, "i");
MODULE_PARM(lpfc0_xmt_que_size, "i");
MODULE_PARM(lpfc0_scan_down, "i");
MODULE_PARM(lpfc0_linkdown_tmo, "i");
MODULE_PARM(lpfc0_nodev_tmo, "i");
MODULE_PARM(lpfc0_delay_rsp_err, "i");
MODULE_PARM(lpfc0_nodev_holdio, "i");
MODULE_PARM(lpfc0_check_cond_err, "i");
MODULE_PARM(lpfc0_topology, "i");
MODULE_PARM(lpfc0_link_speed, "i");
MODULE_PARM(lpfc0_fcp_class, "i");
MODULE_PARM(lpfc0_use_adisc, "i");
MODULE_PARM(lpfc0_extra_io_tmo, "i");
MODULE_PARM(lpfc0_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc0_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc0_ack0, "i");
MODULE_PARM(lpfc0_automap, "i");
MODULE_PARM(lpfc0_fcp_bind_method, "i");
MODULE_PARM(lpfc0_cr_delay, "i");
MODULE_PARM(lpfc0_cr_count, "i");
MODULE_PARM(lpfc0_fdmi_on, "i");
MODULE_PARM(lpfc0_max_lun, "i");
MODULE_PARM(lpfc0_discovery_threads, "i");
MODULE_PARM(lpfc0_max_target, "i");
MODULE_PARM(lpfc0_scsi_req_tmo, "i");
MODULE_PARM(lpfc0_lun_skip, "i");

MODULE_PARM(lpfc1_log_verbose, "i");
MODULE_PARM(lpfc1_lun_queue_depth, "i");
MODULE_PARM(lpfc1_tgt_queue_depth, "i");
MODULE_PARM(lpfc1_no_device_delay, "i");
MODULE_PARM(lpfc1_xmt_que_size, "i");
MODULE_PARM(lpfc1_scan_down, "i");
MODULE_PARM(lpfc1_linkdown_tmo, "i");
MODULE_PARM(lpfc1_nodev_tmo, "i");
MODULE_PARM(lpfc1_delay_rsp_err, "i");
MODULE_PARM(lpfc1_nodev_holdio, "i");
MODULE_PARM(lpfc1_check_cond_err, "i");
MODULE_PARM(lpfc1_topology, "i");
MODULE_PARM(lpfc1_link_speed, "i");
MODULE_PARM(lpfc1_fcp_class, "i");
MODULE_PARM(lpfc1_use_adisc, "i");
MODULE_PARM(lpfc1_extra_io_tmo, "i");
MODULE_PARM(lpfc1_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc1_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc1_ack0, "i");
MODULE_PARM(lpfc1_automap, "i");
MODULE_PARM(lpfc1_fcp_bind_method, "i");
MODULE_PARM(lpfc1_cr_delay, "i");
MODULE_PARM(lpfc1_cr_count, "i");
MODULE_PARM(lpfc1_fdmi_on, "i");
MODULE_PARM(lpfc1_max_lun, "i");
MODULE_PARM(lpfc1_discovery_threads, "i");
MODULE_PARM(lpfc1_max_target, "i");
MODULE_PARM(lpfc1_scsi_req_tmo, "i");
MODULE_PARM(lpfc1_lun_skip, "i");

MODULE_PARM(lpfc2_log_verbose, "i");
MODULE_PARM(lpfc2_lun_queue_depth, "i");
MODULE_PARM(lpfc2_tgt_queue_depth, "i");
MODULE_PARM(lpfc2_no_device_delay, "i");
MODULE_PARM(lpfc2_xmt_que_size, "i");
MODULE_PARM(lpfc2_scan_down, "i");
MODULE_PARM(lpfc2_linkdown_tmo, "i");
MODULE_PARM(lpfc2_nodev_tmo, "i");
MODULE_PARM(lpfc2_delay_rsp_err, "i");
MODULE_PARM(lpfc2_nodev_holdio, "i");
MODULE_PARM(lpfc2_check_cond_err, "i");
MODULE_PARM(lpfc2_topology, "i");
MODULE_PARM(lpfc2_link_speed, "i");
MODULE_PARM(lpfc2_fcp_class, "i");
MODULE_PARM(lpfc2_use_adisc, "i");
MODULE_PARM(lpfc2_extra_io_tmo, "i");
MODULE_PARM(lpfc2_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc2_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc2_ack0, "i");
MODULE_PARM(lpfc2_automap, "i");
MODULE_PARM(lpfc2_fcp_bind_method, "i");
MODULE_PARM(lpfc2_cr_delay, "i");
MODULE_PARM(lpfc2_cr_count, "i");
MODULE_PARM(lpfc2_fdmi_on, "i");
MODULE_PARM(lpfc2_max_lun, "i");
MODULE_PARM(lpfc2_discovery_threads, "i");
MODULE_PARM(lpfc2_max_target, "i");
MODULE_PARM(lpfc2_scsi_req_tmo, "i");
MODULE_PARM(lpfc2_lun_skip, "i");

MODULE_PARM(lpfc3_log_verbose, "i");
MODULE_PARM(lpfc3_lun_queue_depth, "i");
MODULE_PARM(lpfc3_tgt_queue_depth, "i");
MODULE_PARM(lpfc3_no_device_delay, "i");
MODULE_PARM(lpfc3_xmt_que_size, "i");
MODULE_PARM(lpfc3_scan_down, "i");
MODULE_PARM(lpfc3_linkdown_tmo, "i");
MODULE_PARM(lpfc3_nodev_tmo, "i");
MODULE_PARM(lpfc3_delay_rsp_err, "i");
MODULE_PARM(lpfc3_nodev_holdio, "i");
MODULE_PARM(lpfc3_check_cond_err, "i");
MODULE_PARM(lpfc3_topology, "i");
MODULE_PARM(lpfc3_link_speed, "i");
MODULE_PARM(lpfc3_fcp_class, "i");
MODULE_PARM(lpfc3_use_adisc, "i");
MODULE_PARM(lpfc3_extra_io_tmo, "i");
MODULE_PARM(lpfc3_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc3_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc3_ack0, "i");
MODULE_PARM(lpfc3_automap, "i");
MODULE_PARM(lpfc3_fcp_bind_method, "i");
MODULE_PARM(lpfc3_cr_delay, "i");
MODULE_PARM(lpfc3_cr_count, "i");
MODULE_PARM(lpfc3_fdmi_on, "i");
MODULE_PARM(lpfc3_max_lun, "i");
MODULE_PARM(lpfc3_discovery_threads, "i");
MODULE_PARM(lpfc3_max_target, "i");
MODULE_PARM(lpfc3_scsi_req_tmo, "i");
MODULE_PARM(lpfc3_lun_skip, "i");

MODULE_PARM(lpfc4_log_verbose, "i");
MODULE_PARM(lpfc4_lun_queue_depth, "i");
MODULE_PARM(lpfc4_tgt_queue_depth, "i");
MODULE_PARM(lpfc4_no_device_delay, "i");
MODULE_PARM(lpfc4_xmt_que_size, "i");
MODULE_PARM(lpfc4_scan_down, "i");
MODULE_PARM(lpfc4_linkdown_tmo, "i");
MODULE_PARM(lpfc4_nodev_tmo, "i");
MODULE_PARM(lpfc4_delay_rsp_err, "i");
MODULE_PARM(lpfc4_nodev_holdio, "i");
MODULE_PARM(lpfc4_check_cond_err, "i");
MODULE_PARM(lpfc4_topology, "i");
MODULE_PARM(lpfc4_link_speed, "i");
MODULE_PARM(lpfc4_fcp_class, "i");
MODULE_PARM(lpfc4_use_adisc, "i");
MODULE_PARM(lpfc4_extra_io_tmo, "i");
MODULE_PARM(lpfc4_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc4_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc4_ack0, "i");
MODULE_PARM(lpfc4_automap, "i");
MODULE_PARM(lpfc4_fcp_bind_method, "i");
MODULE_PARM(lpfc4_cr_delay, "i");
MODULE_PARM(lpfc4_cr_count, "i");
MODULE_PARM(lpfc4_fdmi_on, "i");
MODULE_PARM(lpfc4_max_lun, "i");
MODULE_PARM(lpfc4_discovery_threads, "i");
MODULE_PARM(lpfc4_max_target, "i");
MODULE_PARM(lpfc4_scsi_req_tmo, "i");
MODULE_PARM(lpfc4_lun_skip, "i");

MODULE_PARM(lpfc5_log_verbose, "i");
MODULE_PARM(lpfc5_lun_queue_depth, "i");
MODULE_PARM(lpfc5_tgt_queue_depth, "i");
MODULE_PARM(lpfc5_no_device_delay, "i");
MODULE_PARM(lpfc5_xmt_que_size, "i");
MODULE_PARM(lpfc5_scan_down, "i");
MODULE_PARM(lpfc5_linkdown_tmo, "i");
MODULE_PARM(lpfc5_nodev_tmo, "i");
MODULE_PARM(lpfc5_delay_rsp_err, "i");
MODULE_PARM(lpfc5_nodev_holdio, "i");
MODULE_PARM(lpfc5_check_cond_err, "i");
MODULE_PARM(lpfc5_topology, "i");
MODULE_PARM(lpfc5_link_speed, "i");
MODULE_PARM(lpfc5_fcp_class, "i");
MODULE_PARM(lpfc5_use_adisc, "i");
MODULE_PARM(lpfc5_extra_io_tmo, "i");
MODULE_PARM(lpfc5_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc5_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc5_ack0, "i");
MODULE_PARM(lpfc5_automap, "i");
MODULE_PARM(lpfc5_fcp_bind_method, "i");
MODULE_PARM(lpfc5_cr_delay, "i");
MODULE_PARM(lpfc5_cr_count, "i");
MODULE_PARM(lpfc5_fdmi_on, "i");
MODULE_PARM(lpfc5_max_lun, "i");
MODULE_PARM(lpfc5_discovery_threads, "i");
MODULE_PARM(lpfc5_max_target, "i");
MODULE_PARM(lpfc5_scsi_req_tmo, "i");
MODULE_PARM(lpfc5_lun_skip, "i");

MODULE_PARM(lpfc6_log_verbose, "i");
MODULE_PARM(lpfc6_lun_queue_depth, "i");
MODULE_PARM(lpfc6_tgt_queue_depth, "i");
MODULE_PARM(lpfc6_no_device_delay, "i");
MODULE_PARM(lpfc6_xmt_que_size, "i");
MODULE_PARM(lpfc6_scan_down, "i");
MODULE_PARM(lpfc6_linkdown_tmo, "i");
MODULE_PARM(lpfc6_nodev_tmo, "i");
MODULE_PARM(lpfc6_delay_rsp_err, "i");
MODULE_PARM(lpfc6_nodev_holdio, "i");
MODULE_PARM(lpfc6_check_cond_err, "i");
MODULE_PARM(lpfc6_topology, "i");
MODULE_PARM(lpfc6_link_speed, "i");
MODULE_PARM(lpfc6_fcp_class, "i");
MODULE_PARM(lpfc6_use_adisc, "i");
MODULE_PARM(lpfc6_extra_io_tmo, "i");
MODULE_PARM(lpfc6_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc6_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc6_ack0, "i");
MODULE_PARM(lpfc6_automap, "i");
MODULE_PARM(lpfc6_fcp_bind_method, "i");
MODULE_PARM(lpfc6_cr_delay, "i");
MODULE_PARM(lpfc6_cr_count, "i");
MODULE_PARM(lpfc6_fdmi_on, "i");
MODULE_PARM(lpfc6_max_lun, "i");
MODULE_PARM(lpfc6_discovery_threads, "i");
MODULE_PARM(lpfc6_max_target, "i");
MODULE_PARM(lpfc6_scsi_req_tmo, "i");
MODULE_PARM(lpfc6_lun_skip, "i");

MODULE_PARM(lpfc7_log_verbose, "i");
MODULE_PARM(lpfc7_lun_queue_depth, "i");
MODULE_PARM(lpfc7_tgt_queue_depth, "i");
MODULE_PARM(lpfc7_no_device_delay, "i");
MODULE_PARM(lpfc7_xmt_que_size, "i");
MODULE_PARM(lpfc7_scan_down, "i");
MODULE_PARM(lpfc7_linkdown_tmo, "i");
MODULE_PARM(lpfc7_nodev_tmo, "i");
MODULE_PARM(lpfc7_delay_rsp_err, "i");
MODULE_PARM(lpfc7_nodev_holdio, "i");
MODULE_PARM(lpfc7_check_cond_err, "i");
MODULE_PARM(lpfc7_topology, "i");
MODULE_PARM(lpfc7_link_speed, "i");
MODULE_PARM(lpfc7_fcp_class, "i");
MODULE_PARM(lpfc7_use_adisc, "i");
MODULE_PARM(lpfc7_extra_io_tmo, "i");
MODULE_PARM(lpfc7_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc7_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc7_ack0, "i");
MODULE_PARM(lpfc7_automap, "i");
MODULE_PARM(lpfc7_fcp_bind_method, "i");
MODULE_PARM(lpfc7_cr_delay, "i");
MODULE_PARM(lpfc7_cr_count, "i");
MODULE_PARM(lpfc7_fdmi_on, "i");
MODULE_PARM(lpfc7_max_lun, "i");
MODULE_PARM(lpfc7_discovery_threads, "i");
MODULE_PARM(lpfc7_max_target, "i");
MODULE_PARM(lpfc7_scsi_req_tmo, "i");
MODULE_PARM(lpfc7_lun_skip, "i");

MODULE_PARM(lpfc8_log_verbose, "i");
MODULE_PARM(lpfc8_lun_queue_depth, "i");
MODULE_PARM(lpfc8_tgt_queue_depth, "i");
MODULE_PARM(lpfc8_no_device_delay, "i");
MODULE_PARM(lpfc8_xmt_que_size, "i");
MODULE_PARM(lpfc8_scan_down, "i");
MODULE_PARM(lpfc8_linkdown_tmo, "i");
MODULE_PARM(lpfc8_nodev_tmo, "i");
MODULE_PARM(lpfc8_delay_rsp_err, "i");
MODULE_PARM(lpfc8_nodev_holdio, "i");
MODULE_PARM(lpfc8_check_cond_err, "i");
MODULE_PARM(lpfc8_topology, "i");
MODULE_PARM(lpfc8_link_speed, "i");
MODULE_PARM(lpfc8_fcp_class, "i");
MODULE_PARM(lpfc8_use_adisc, "i");
MODULE_PARM(lpfc8_extra_io_tmo, "i");
MODULE_PARM(lpfc8_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc8_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc8_ack0, "i");
MODULE_PARM(lpfc8_automap, "i");
MODULE_PARM(lpfc8_fcp_bind_method, "i");
MODULE_PARM(lpfc8_cr_delay, "i");
MODULE_PARM(lpfc8_cr_count, "i");
MODULE_PARM(lpfc8_fdmi_on, "i");
MODULE_PARM(lpfc8_max_lun, "i");
MODULE_PARM(lpfc8_discovery_threads, "i");
MODULE_PARM(lpfc8_max_target, "i");
MODULE_PARM(lpfc8_scsi_req_tmo, "i");
MODULE_PARM(lpfc8_lun_skip, "i");

MODULE_PARM(lpfc9_log_verbose, "i");
MODULE_PARM(lpfc9_lun_queue_depth, "i");
MODULE_PARM(lpfc9_tgt_queue_depth, "i");
MODULE_PARM(lpfc9_no_device_delay, "i");
MODULE_PARM(lpfc9_xmt_que_size, "i");
MODULE_PARM(lpfc9_scan_down, "i");
MODULE_PARM(lpfc9_linkdown_tmo, "i");
MODULE_PARM(lpfc9_nodev_tmo, "i");
MODULE_PARM(lpfc9_delay_rsp_err, "i");
MODULE_PARM(lpfc9_nodev_holdio, "i");
MODULE_PARM(lpfc9_check_cond_err, "i");
MODULE_PARM(lpfc9_topology, "i");
MODULE_PARM(lpfc9_link_speed, "i");
MODULE_PARM(lpfc9_fcp_class, "i");
MODULE_PARM(lpfc9_use_adisc, "i");
MODULE_PARM(lpfc9_extra_io_tmo, "i");
MODULE_PARM(lpfc9_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc9_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc9_ack0, "i");
MODULE_PARM(lpfc9_automap, "i");
MODULE_PARM(lpfc9_fcp_bind_method, "i");
MODULE_PARM(lpfc9_cr_delay, "i");
MODULE_PARM(lpfc9_cr_count, "i");
MODULE_PARM(lpfc9_fdmi_on, "i");
MODULE_PARM(lpfc9_max_lun, "i");
MODULE_PARM(lpfc9_discovery_threads, "i");
MODULE_PARM(lpfc9_max_target, "i");
MODULE_PARM(lpfc9_scsi_req_tmo, "i");
MODULE_PARM(lpfc9_lun_skip, "i");

MODULE_PARM(lpfc10_log_verbose, "i");
MODULE_PARM(lpfc10_lun_queue_depth, "i");
MODULE_PARM(lpfc10_tgt_queue_depth, "i");
MODULE_PARM(lpfc10_no_device_delay, "i");
MODULE_PARM(lpfc10_xmt_que_size, "i");
MODULE_PARM(lpfc10_scan_down, "i");
MODULE_PARM(lpfc10_linkdown_tmo, "i");
MODULE_PARM(lpfc10_nodev_tmo, "i");
MODULE_PARM(lpfc10_delay_rsp_err, "i");
MODULE_PARM(lpfc10_nodev_holdio, "i");
MODULE_PARM(lpfc10_check_cond_err, "i");
MODULE_PARM(lpfc10_topology, "i");
MODULE_PARM(lpfc10_link_speed, "i");
MODULE_PARM(lpfc10_fcp_class, "i");
MODULE_PARM(lpfc10_use_adisc, "i");
MODULE_PARM(lpfc10_extra_io_tmo, "i");
MODULE_PARM(lpfc10_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc10_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc10_ack0, "i");
MODULE_PARM(lpfc10_automap, "i");
MODULE_PARM(lpfc10_fcp_bind_method, "i");
MODULE_PARM(lpfc10_cr_delay, "i");
MODULE_PARM(lpfc10_cr_count, "i");
MODULE_PARM(lpfc10_fdmi_on, "i");
MODULE_PARM(lpfc10_max_lun, "i");
MODULE_PARM(lpfc10_discovery_threads, "i");
MODULE_PARM(lpfc10_max_target, "i");
MODULE_PARM(lpfc10_scsi_req_tmo, "i");
MODULE_PARM(lpfc10_lun_skip, "i");

MODULE_PARM(lpfc11_log_verbose, "i");
MODULE_PARM(lpfc11_lun_queue_depth, "i");
MODULE_PARM(lpfc11_tgt_queue_depth, "i");
MODULE_PARM(lpfc11_no_device_delay, "i");
MODULE_PARM(lpfc11_xmt_que_size, "i");
MODULE_PARM(lpfc11_scan_down, "i");
MODULE_PARM(lpfc11_linkdown_tmo, "i");
MODULE_PARM(lpfc11_nodev_tmo, "i");
MODULE_PARM(lpfc11_delay_rsp_err, "i");
MODULE_PARM(lpfc11_nodev_holdio, "i");
MODULE_PARM(lpfc11_check_cond_err, "i");
MODULE_PARM(lpfc11_topology, "i");
MODULE_PARM(lpfc11_link_speed, "i");
MODULE_PARM(lpfc11_fcp_class, "i");
MODULE_PARM(lpfc11_use_adisc, "i");
MODULE_PARM(lpfc11_extra_io_tmo, "i");
MODULE_PARM(lpfc11_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc11_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc11_ack0, "i");
MODULE_PARM(lpfc11_automap, "i");
MODULE_PARM(lpfc11_fcp_bind_method, "i");
MODULE_PARM(lpfc11_cr_delay, "i");
MODULE_PARM(lpfc11_cr_count, "i");
MODULE_PARM(lpfc11_fdmi_on, "i");
MODULE_PARM(lpfc11_max_lun, "i");
MODULE_PARM(lpfc11_discovery_threads, "i");
MODULE_PARM(lpfc11_max_target, "i");
MODULE_PARM(lpfc11_scsi_req_tmo, "i");
MODULE_PARM(lpfc11_lun_skip, "i");

MODULE_PARM(lpfc12_log_verbose, "i");
MODULE_PARM(lpfc12_lun_queue_depth, "i");
MODULE_PARM(lpfc12_tgt_queue_depth, "i");
MODULE_PARM(lpfc12_no_device_delay, "i");
MODULE_PARM(lpfc12_xmt_que_size, "i");
MODULE_PARM(lpfc12_scan_down, "i");
MODULE_PARM(lpfc12_linkdown_tmo, "i");
MODULE_PARM(lpfc12_nodev_tmo, "i");
MODULE_PARM(lpfc12_delay_rsp_err, "i");
MODULE_PARM(lpfc12_nodev_holdio, "i");
MODULE_PARM(lpfc12_check_cond_err, "i");
MODULE_PARM(lpfc12_topology, "i");
MODULE_PARM(lpfc12_link_speed, "i");
MODULE_PARM(lpfc12_fcp_class, "i");
MODULE_PARM(lpfc12_use_adisc, "i");
MODULE_PARM(lpfc12_extra_io_tmo, "i");
MODULE_PARM(lpfc12_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc12_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc12_ack0, "i");
MODULE_PARM(lpfc12_automap, "i");
MODULE_PARM(lpfc12_fcp_bind_method, "i");
MODULE_PARM(lpfc12_cr_delay, "i");
MODULE_PARM(lpfc12_cr_count, "i");
MODULE_PARM(lpfc12_fdmi_on, "i");
MODULE_PARM(lpfc12_max_lun, "i");
MODULE_PARM(lpfc12_discovery_threads, "i");
MODULE_PARM(lpfc12_max_target, "i");
MODULE_PARM(lpfc12_scsi_req_tmo, "i");
MODULE_PARM(lpfc12_lun_skip, "i");

MODULE_PARM(lpfc13_log_verbose, "i");
MODULE_PARM(lpfc13_lun_queue_depth, "i");
MODULE_PARM(lpfc13_tgt_queue_depth, "i");
MODULE_PARM(lpfc13_no_device_delay, "i");
MODULE_PARM(lpfc13_xmt_que_size, "i");
MODULE_PARM(lpfc13_scan_down, "i");
MODULE_PARM(lpfc13_linkdown_tmo, "i");
MODULE_PARM(lpfc13_nodev_tmo, "i");
MODULE_PARM(lpfc13_delay_rsp_err, "i");
MODULE_PARM(lpfc13_nodev_holdio, "i");
MODULE_PARM(lpfc13_check_cond_err, "i");
MODULE_PARM(lpfc13_topology, "i");
MODULE_PARM(lpfc13_link_speed, "i");
MODULE_PARM(lpfc13_fcp_class, "i");
MODULE_PARM(lpfc13_use_adisc, "i");
MODULE_PARM(lpfc13_extra_io_tmo, "i");
MODULE_PARM(lpfc13_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc13_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc13_ack0, "i");
MODULE_PARM(lpfc13_automap, "i");
MODULE_PARM(lpfc13_fcp_bind_method, "i");
MODULE_PARM(lpfc13_cr_delay, "i");
MODULE_PARM(lpfc13_cr_count, "i");
MODULE_PARM(lpfc13_fdmi_on, "i");
MODULE_PARM(lpfc13_max_lun, "i");
MODULE_PARM(lpfc13_discovery_threads, "i");
MODULE_PARM(lpfc13_max_target, "i");
MODULE_PARM(lpfc13_scsi_req_tmo, "i");
MODULE_PARM(lpfc13_lun_skip, "i");

MODULE_PARM(lpfc14_log_verbose, "i");
MODULE_PARM(lpfc14_lun_queue_depth, "i");
MODULE_PARM(lpfc14_tgt_queue_depth, "i");
MODULE_PARM(lpfc14_no_device_delay, "i");
MODULE_PARM(lpfc14_xmt_que_size, "i");
MODULE_PARM(lpfc14_scan_down, "i");
MODULE_PARM(lpfc14_linkdown_tmo, "i");
MODULE_PARM(lpfc14_nodev_tmo, "i");
MODULE_PARM(lpfc14_delay_rsp_err, "i");
MODULE_PARM(lpfc14_nodev_holdio, "i");
MODULE_PARM(lpfc14_check_cond_err, "i");
MODULE_PARM(lpfc14_topology, "i");
MODULE_PARM(lpfc14_link_speed, "i");
MODULE_PARM(lpfc14_fcp_class, "i");
MODULE_PARM(lpfc14_use_adisc, "i");
MODULE_PARM(lpfc14_extra_io_tmo, "i");
MODULE_PARM(lpfc14_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc14_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc14_ack0, "i");
MODULE_PARM(lpfc14_automap, "i");
MODULE_PARM(lpfc14_fcp_bind_method, "i");
MODULE_PARM(lpfc14_cr_delay, "i");
MODULE_PARM(lpfc14_cr_count, "i");
MODULE_PARM(lpfc14_fdmi_on, "i");
MODULE_PARM(lpfc14_max_lun, "i");
MODULE_PARM(lpfc14_discovery_threads, "i");
MODULE_PARM(lpfc14_max_target, "i");
MODULE_PARM(lpfc14_scsi_req_tmo, "i");
MODULE_PARM(lpfc14_lun_skip, "i");

MODULE_PARM(lpfc15_log_verbose, "i");
MODULE_PARM(lpfc15_lun_queue_depth, "i");
MODULE_PARM(lpfc15_tgt_queue_depth, "i");
MODULE_PARM(lpfc15_no_device_delay, "i");
MODULE_PARM(lpfc15_xmt_que_size, "i");
MODULE_PARM(lpfc15_scan_down, "i");
MODULE_PARM(lpfc15_linkdown_tmo, "i");
MODULE_PARM(lpfc15_nodev_tmo, "i");
MODULE_PARM(lpfc15_delay_rsp_err, "i");
MODULE_PARM(lpfc15_nodev_holdio, "i");
MODULE_PARM(lpfc15_check_cond_err, "i");
MODULE_PARM(lpfc15_topology, "i");
MODULE_PARM(lpfc15_link_speed, "i");
MODULE_PARM(lpfc15_fcp_class, "i");
MODULE_PARM(lpfc15_use_adisc, "i");
MODULE_PARM(lpfc15_extra_io_tmo, "i");
MODULE_PARM(lpfc15_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc15_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc15_ack0, "i");
MODULE_PARM(lpfc15_automap, "i");
MODULE_PARM(lpfc15_fcp_bind_method, "i");
MODULE_PARM(lpfc15_cr_delay, "i");
MODULE_PARM(lpfc15_cr_count, "i");
MODULE_PARM(lpfc15_fdmi_on, "i");
MODULE_PARM(lpfc15_max_lun, "i");
MODULE_PARM(lpfc15_discovery_threads, "i");
MODULE_PARM(lpfc15_max_target, "i");
MODULE_PARM(lpfc15_scsi_req_tmo, "i");
MODULE_PARM(lpfc15_lun_skip, "i");

MODULE_PARM(lpfc16_log_verbose, "i");
MODULE_PARM(lpfc16_lun_queue_depth, "i");
MODULE_PARM(lpfc16_tgt_queue_depth, "i");
MODULE_PARM(lpfc16_no_device_delay, "i");
MODULE_PARM(lpfc16_xmt_que_size, "i");
MODULE_PARM(lpfc16_scan_down, "i");
MODULE_PARM(lpfc16_linkdown_tmo, "i");
MODULE_PARM(lpfc16_nodev_tmo, "i");
MODULE_PARM(lpfc16_delay_rsp_err, "i");
MODULE_PARM(lpfc16_nodev_holdio, "i");
MODULE_PARM(lpfc16_check_cond_err, "i");
MODULE_PARM(lpfc16_topology, "i");
MODULE_PARM(lpfc16_link_speed, "i");
MODULE_PARM(lpfc16_fcp_class, "i");
MODULE_PARM(lpfc16_use_adisc, "i");
MODULE_PARM(lpfc16_extra_io_tmo, "i");
MODULE_PARM(lpfc16_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc16_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc16_ack0, "i");
MODULE_PARM(lpfc16_automap, "i");
MODULE_PARM(lpfc16_fcp_bind_method, "i");
MODULE_PARM(lpfc16_cr_delay, "i");
MODULE_PARM(lpfc16_cr_count, "i");
MODULE_PARM(lpfc16_fdmi_on, "i");
MODULE_PARM(lpfc16_max_lun, "i");
MODULE_PARM(lpfc16_discovery_threads, "i");
MODULE_PARM(lpfc16_max_target, "i");
MODULE_PARM(lpfc16_scsi_req_tmo, "i");
MODULE_PARM(lpfc16_lun_skip, "i");

MODULE_PARM(lpfc17_log_verbose, "i");
MODULE_PARM(lpfc17_lun_queue_depth, "i");
MODULE_PARM(lpfc17_tgt_queue_depth, "i");
MODULE_PARM(lpfc17_no_device_delay, "i");
MODULE_PARM(lpfc17_xmt_que_size, "i");
MODULE_PARM(lpfc17_scan_down, "i");
MODULE_PARM(lpfc17_linkdown_tmo, "i");
MODULE_PARM(lpfc17_nodev_tmo, "i");
MODULE_PARM(lpfc17_delay_rsp_err, "i");
MODULE_PARM(lpfc17_nodev_holdio, "i");
MODULE_PARM(lpfc17_check_cond_err, "i");
MODULE_PARM(lpfc17_topology, "i");
MODULE_PARM(lpfc17_link_speed, "i");
MODULE_PARM(lpfc17_fcp_class, "i");
MODULE_PARM(lpfc17_use_adisc, "i");
MODULE_PARM(lpfc17_extra_io_tmo, "i");
MODULE_PARM(lpfc17_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc17_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc17_ack0, "i");
MODULE_PARM(lpfc17_automap, "i");
MODULE_PARM(lpfc17_fcp_bind_method, "i");
MODULE_PARM(lpfc17_cr_delay, "i");
MODULE_PARM(lpfc17_cr_count, "i");
MODULE_PARM(lpfc17_fdmi_on, "i");
MODULE_PARM(lpfc17_max_lun, "i");
MODULE_PARM(lpfc17_discovery_threads, "i");
MODULE_PARM(lpfc17_max_target, "i");
MODULE_PARM(lpfc17_scsi_req_tmo, "i");
MODULE_PARM(lpfc17_lun_skip, "i");

MODULE_PARM(lpfc18_log_verbose, "i");
MODULE_PARM(lpfc18_lun_queue_depth, "i");
MODULE_PARM(lpfc18_tgt_queue_depth, "i");
MODULE_PARM(lpfc18_no_device_delay, "i");
MODULE_PARM(lpfc18_xmt_que_size, "i");
MODULE_PARM(lpfc18_scan_down, "i");
MODULE_PARM(lpfc18_linkdown_tmo, "i");
MODULE_PARM(lpfc18_nodev_tmo, "i");
MODULE_PARM(lpfc18_delay_rsp_err, "i");
MODULE_PARM(lpfc18_nodev_holdio, "i");
MODULE_PARM(lpfc18_check_cond_err, "i");
MODULE_PARM(lpfc18_topology, "i");
MODULE_PARM(lpfc18_link_speed, "i");
MODULE_PARM(lpfc18_fcp_class, "i");
MODULE_PARM(lpfc18_use_adisc, "i");
MODULE_PARM(lpfc18_extra_io_tmo, "i");
MODULE_PARM(lpfc18_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc18_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc18_ack0, "i");
MODULE_PARM(lpfc18_automap, "i");
MODULE_PARM(lpfc18_fcp_bind_method, "i");
MODULE_PARM(lpfc18_cr_delay, "i");
MODULE_PARM(lpfc18_cr_count, "i");
MODULE_PARM(lpfc18_fdmi_on, "i");
MODULE_PARM(lpfc18_max_lun, "i");
MODULE_PARM(lpfc18_discovery_threads, "i");
MODULE_PARM(lpfc18_max_target, "i");
MODULE_PARM(lpfc18_scsi_req_tmo, "i");
MODULE_PARM(lpfc18_lun_skip, "i");

MODULE_PARM(lpfc19_log_verbose, "i");
MODULE_PARM(lpfc19_lun_queue_depth, "i");
MODULE_PARM(lpfc19_tgt_queue_depth, "i");
MODULE_PARM(lpfc19_no_device_delay, "i");
MODULE_PARM(lpfc19_xmt_que_size, "i");
MODULE_PARM(lpfc19_scan_down, "i");
MODULE_PARM(lpfc19_linkdown_tmo, "i");
MODULE_PARM(lpfc19_nodev_tmo, "i");
MODULE_PARM(lpfc19_delay_rsp_err, "i");
MODULE_PARM(lpfc19_nodev_holdio, "i");
MODULE_PARM(lpfc19_check_cond_err, "i");
MODULE_PARM(lpfc19_topology, "i");
MODULE_PARM(lpfc19_link_speed, "i");
MODULE_PARM(lpfc19_fcp_class, "i");
MODULE_PARM(lpfc19_use_adisc, "i");
MODULE_PARM(lpfc19_extra_io_tmo, "i");
MODULE_PARM(lpfc19_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc19_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc19_ack0, "i");
MODULE_PARM(lpfc19_automap, "i");
MODULE_PARM(lpfc19_fcp_bind_method, "i");
MODULE_PARM(lpfc19_cr_delay, "i");
MODULE_PARM(lpfc19_cr_count, "i");
MODULE_PARM(lpfc19_fdmi_on, "i");
MODULE_PARM(lpfc19_max_lun, "i");
MODULE_PARM(lpfc19_discovery_threads, "i");
MODULE_PARM(lpfc19_max_target, "i");
MODULE_PARM(lpfc19_scsi_req_tmo, "i");
MODULE_PARM(lpfc19_lun_skip, "i");

MODULE_PARM(lpfc20_log_verbose, "i");
MODULE_PARM(lpfc20_lun_queue_depth, "i");
MODULE_PARM(lpfc20_tgt_queue_depth, "i");
MODULE_PARM(lpfc20_no_device_delay, "i");
MODULE_PARM(lpfc20_xmt_que_size, "i");
MODULE_PARM(lpfc20_scan_down, "i");
MODULE_PARM(lpfc20_linkdown_tmo, "i");
MODULE_PARM(lpfc20_nodev_tmo, "i");
MODULE_PARM(lpfc20_delay_rsp_err, "i");
MODULE_PARM(lpfc20_nodev_holdio, "i");
MODULE_PARM(lpfc20_check_cond_err, "i");
MODULE_PARM(lpfc20_topology, "i");
MODULE_PARM(lpfc20_link_speed, "i");
MODULE_PARM(lpfc20_fcp_class, "i");
MODULE_PARM(lpfc20_use_adisc, "i");
MODULE_PARM(lpfc20_extra_io_tmo, "i");
MODULE_PARM(lpfc20_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc20_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc20_ack0, "i");
MODULE_PARM(lpfc20_automap, "i");
MODULE_PARM(lpfc20_fcp_bind_method, "i");
MODULE_PARM(lpfc20_cr_delay, "i");
MODULE_PARM(lpfc20_cr_count, "i");
MODULE_PARM(lpfc20_fdmi_on, "i");
MODULE_PARM(lpfc20_max_lun, "i");
MODULE_PARM(lpfc20_discovery_threads, "i");
MODULE_PARM(lpfc20_max_target, "i");
MODULE_PARM(lpfc20_scsi_req_tmo, "i");
MODULE_PARM(lpfc20_lun_skip, "i");

MODULE_PARM(lpfc21_log_verbose, "i");
MODULE_PARM(lpfc21_lun_queue_depth, "i");
MODULE_PARM(lpfc21_tgt_queue_depth, "i");
MODULE_PARM(lpfc21_no_device_delay, "i");
MODULE_PARM(lpfc21_xmt_que_size, "i");
MODULE_PARM(lpfc21_scan_down, "i");
MODULE_PARM(lpfc21_linkdown_tmo, "i");
MODULE_PARM(lpfc21_nodev_tmo, "i");
MODULE_PARM(lpfc21_delay_rsp_err, "i");
MODULE_PARM(lpfc21_nodev_holdio, "i");
MODULE_PARM(lpfc21_check_cond_err, "i");
MODULE_PARM(lpfc21_topology, "i");
MODULE_PARM(lpfc21_link_speed, "i");
MODULE_PARM(lpfc21_fcp_class, "i");
MODULE_PARM(lpfc21_use_adisc, "i");
MODULE_PARM(lpfc21_extra_io_tmo, "i");
MODULE_PARM(lpfc21_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc21_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc21_ack0, "i");
MODULE_PARM(lpfc21_automap, "i");
MODULE_PARM(lpfc21_fcp_bind_method, "i");
MODULE_PARM(lpfc21_cr_delay, "i");
MODULE_PARM(lpfc21_cr_count, "i");
MODULE_PARM(lpfc21_fdmi_on, "i");
MODULE_PARM(lpfc21_max_lun, "i");
MODULE_PARM(lpfc21_discovery_threads, "i");
MODULE_PARM(lpfc21_max_target, "i");
MODULE_PARM(lpfc21_scsi_req_tmo, "i");
MODULE_PARM(lpfc21_lun_skip, "i");

MODULE_PARM(lpfc22_log_verbose, "i");
MODULE_PARM(lpfc22_lun_queue_depth, "i");
MODULE_PARM(lpfc22_tgt_queue_depth, "i");
MODULE_PARM(lpfc22_no_device_delay, "i");
MODULE_PARM(lpfc22_xmt_que_size, "i");
MODULE_PARM(lpfc22_scan_down, "i");
MODULE_PARM(lpfc22_linkdown_tmo, "i");
MODULE_PARM(lpfc22_nodev_tmo, "i");
MODULE_PARM(lpfc22_delay_rsp_err, "i");
MODULE_PARM(lpfc22_nodev_holdio, "i");
MODULE_PARM(lpfc22_check_cond_err, "i");
MODULE_PARM(lpfc22_topology, "i");
MODULE_PARM(lpfc22_link_speed, "i");
MODULE_PARM(lpfc22_fcp_class, "i");
MODULE_PARM(lpfc22_use_adisc, "i");
MODULE_PARM(lpfc22_extra_io_tmo, "i");
MODULE_PARM(lpfc22_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc22_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc22_ack0, "i");
MODULE_PARM(lpfc22_automap, "i");
MODULE_PARM(lpfc22_fcp_bind_method, "i");
MODULE_PARM(lpfc22_cr_delay, "i");
MODULE_PARM(lpfc22_cr_count, "i");
MODULE_PARM(lpfc22_fdmi_on, "i");
MODULE_PARM(lpfc22_max_lun, "i");
MODULE_PARM(lpfc22_discovery_threads, "i");
MODULE_PARM(lpfc22_max_target, "i");
MODULE_PARM(lpfc22_scsi_req_tmo, "i");
MODULE_PARM(lpfc22_lun_skip, "i");

MODULE_PARM(lpfc23_log_verbose, "i");
MODULE_PARM(lpfc23_lun_queue_depth, "i");
MODULE_PARM(lpfc23_tgt_queue_depth, "i");
MODULE_PARM(lpfc23_no_device_delay, "i");
MODULE_PARM(lpfc23_xmt_que_size, "i");
MODULE_PARM(lpfc23_scan_down, "i");
MODULE_PARM(lpfc23_linkdown_tmo, "i");
MODULE_PARM(lpfc23_nodev_tmo, "i");
MODULE_PARM(lpfc23_delay_rsp_err, "i");
MODULE_PARM(lpfc23_nodev_holdio, "i");
MODULE_PARM(lpfc23_check_cond_err, "i");
MODULE_PARM(lpfc23_topology, "i");
MODULE_PARM(lpfc23_link_speed, "i");
MODULE_PARM(lpfc23_fcp_class, "i");
MODULE_PARM(lpfc23_use_adisc, "i");
MODULE_PARM(lpfc23_extra_io_tmo, "i");
MODULE_PARM(lpfc23_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc23_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc23_ack0, "i");
MODULE_PARM(lpfc23_automap, "i");
MODULE_PARM(lpfc23_fcp_bind_method, "i");
MODULE_PARM(lpfc23_cr_delay, "i");
MODULE_PARM(lpfc23_cr_count, "i");
MODULE_PARM(lpfc23_fdmi_on, "i");
MODULE_PARM(lpfc23_max_lun, "i");
MODULE_PARM(lpfc23_discovery_threads, "i");
MODULE_PARM(lpfc23_max_target, "i");
MODULE_PARM(lpfc23_scsi_req_tmo, "i");
MODULE_PARM(lpfc23_lun_skip, "i");

MODULE_PARM(lpfc24_log_verbose, "i");
MODULE_PARM(lpfc24_lun_queue_depth, "i");
MODULE_PARM(lpfc24_tgt_queue_depth, "i");
MODULE_PARM(lpfc24_no_device_delay, "i");
MODULE_PARM(lpfc24_xmt_que_size, "i");
MODULE_PARM(lpfc24_scan_down, "i");
MODULE_PARM(lpfc24_linkdown_tmo, "i");
MODULE_PARM(lpfc24_nodev_tmo, "i");
MODULE_PARM(lpfc24_delay_rsp_err, "i");
MODULE_PARM(lpfc24_nodev_holdio, "i");
MODULE_PARM(lpfc24_check_cond_err, "i");
MODULE_PARM(lpfc24_topology, "i");
MODULE_PARM(lpfc24_link_speed, "i");
MODULE_PARM(lpfc24_fcp_class, "i");
MODULE_PARM(lpfc24_use_adisc, "i");
MODULE_PARM(lpfc24_extra_io_tmo, "i");
MODULE_PARM(lpfc24_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc24_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc24_ack0, "i");
MODULE_PARM(lpfc24_automap, "i");
MODULE_PARM(lpfc24_fcp_bind_method, "i");
MODULE_PARM(lpfc24_cr_delay, "i");
MODULE_PARM(lpfc24_cr_count, "i");
MODULE_PARM(lpfc24_fdmi_on, "i");
MODULE_PARM(lpfc24_max_lun, "i");
MODULE_PARM(lpfc24_discovery_threads, "i");
MODULE_PARM(lpfc24_max_target, "i");
MODULE_PARM(lpfc24_scsi_req_tmo, "i");
MODULE_PARM(lpfc24_lun_skip, "i");

MODULE_PARM(lpfc25_log_verbose, "i");
MODULE_PARM(lpfc25_lun_queue_depth, "i");
MODULE_PARM(lpfc25_tgt_queue_depth, "i");
MODULE_PARM(lpfc25_no_device_delay, "i");
MODULE_PARM(lpfc25_xmt_que_size, "i");
MODULE_PARM(lpfc25_scan_down, "i");
MODULE_PARM(lpfc25_linkdown_tmo, "i");
MODULE_PARM(lpfc25_nodev_tmo, "i");
MODULE_PARM(lpfc25_delay_rsp_err, "i");
MODULE_PARM(lpfc25_nodev_holdio, "i");
MODULE_PARM(lpfc25_check_cond_err, "i");
MODULE_PARM(lpfc25_topology, "i");
MODULE_PARM(lpfc25_link_speed, "i");
MODULE_PARM(lpfc25_fcp_class, "i");
MODULE_PARM(lpfc25_use_adisc, "i");
MODULE_PARM(lpfc25_extra_io_tmo, "i");
MODULE_PARM(lpfc25_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc25_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc25_ack0, "i");
MODULE_PARM(lpfc25_automap, "i");
MODULE_PARM(lpfc25_fcp_bind_method, "i");
MODULE_PARM(lpfc25_cr_delay, "i");
MODULE_PARM(lpfc25_cr_count, "i");
MODULE_PARM(lpfc25_fdmi_on, "i");
MODULE_PARM(lpfc25_max_lun, "i");
MODULE_PARM(lpfc25_discovery_threads, "i");
MODULE_PARM(lpfc25_max_target, "i");
MODULE_PARM(lpfc25_scsi_req_tmo, "i");
MODULE_PARM(lpfc25_lun_skip, "i");

MODULE_PARM(lpfc26_log_verbose, "i");
MODULE_PARM(lpfc26_lun_queue_depth, "i");
MODULE_PARM(lpfc26_tgt_queue_depth, "i");
MODULE_PARM(lpfc26_no_device_delay, "i");
MODULE_PARM(lpfc26_xmt_que_size, "i");
MODULE_PARM(lpfc26_scan_down, "i");
MODULE_PARM(lpfc26_linkdown_tmo, "i");
MODULE_PARM(lpfc26_nodev_tmo, "i");
MODULE_PARM(lpfc26_delay_rsp_err, "i");
MODULE_PARM(lpfc26_nodev_holdio, "i");
MODULE_PARM(lpfc26_check_cond_err, "i");
MODULE_PARM(lpfc26_topology, "i");
MODULE_PARM(lpfc26_link_speed, "i");
MODULE_PARM(lpfc26_fcp_class, "i");
MODULE_PARM(lpfc26_use_adisc, "i");
MODULE_PARM(lpfc26_extra_io_tmo, "i");
MODULE_PARM(lpfc26_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc26_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc26_ack0, "i");
MODULE_PARM(lpfc26_automap, "i");
MODULE_PARM(lpfc26_fcp_bind_method, "i");
MODULE_PARM(lpfc26_cr_delay, "i");
MODULE_PARM(lpfc26_cr_count, "i");
MODULE_PARM(lpfc26_fdmi_on, "i");
MODULE_PARM(lpfc26_max_lun, "i");
MODULE_PARM(lpfc26_discovery_threads, "i");
MODULE_PARM(lpfc26_max_target, "i");
MODULE_PARM(lpfc26_scsi_req_tmo, "i");
MODULE_PARM(lpfc26_lun_skip, "i");

MODULE_PARM(lpfc27_log_verbose, "i");
MODULE_PARM(lpfc27_lun_queue_depth, "i");
MODULE_PARM(lpfc27_tgt_queue_depth, "i");
MODULE_PARM(lpfc27_no_device_delay, "i");
MODULE_PARM(lpfc27_xmt_que_size, "i");
MODULE_PARM(lpfc27_scan_down, "i");
MODULE_PARM(lpfc27_linkdown_tmo, "i");
MODULE_PARM(lpfc27_nodev_tmo, "i");
MODULE_PARM(lpfc27_delay_rsp_err, "i");
MODULE_PARM(lpfc27_nodev_holdio, "i");
MODULE_PARM(lpfc27_check_cond_err, "i");
MODULE_PARM(lpfc27_topology, "i");
MODULE_PARM(lpfc27_link_speed, "i");
MODULE_PARM(lpfc27_fcp_class, "i");
MODULE_PARM(lpfc27_use_adisc, "i");
MODULE_PARM(lpfc27_extra_io_tmo, "i");
MODULE_PARM(lpfc27_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc27_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc27_ack0, "i");
MODULE_PARM(lpfc27_automap, "i");
MODULE_PARM(lpfc27_fcp_bind_method, "i");
MODULE_PARM(lpfc27_cr_delay, "i");
MODULE_PARM(lpfc27_cr_count, "i");
MODULE_PARM(lpfc27_fdmi_on, "i");
MODULE_PARM(lpfc27_max_lun, "i");
MODULE_PARM(lpfc27_discovery_threads, "i");
MODULE_PARM(lpfc27_max_target, "i");
MODULE_PARM(lpfc27_scsi_req_tmo, "i");
MODULE_PARM(lpfc27_lun_skip, "i");

MODULE_PARM(lpfc28_log_verbose, "i");
MODULE_PARM(lpfc28_lun_queue_depth, "i");
MODULE_PARM(lpfc28_tgt_queue_depth, "i");
MODULE_PARM(lpfc28_no_device_delay, "i");
MODULE_PARM(lpfc28_xmt_que_size, "i");
MODULE_PARM(lpfc28_scan_down, "i");
MODULE_PARM(lpfc28_linkdown_tmo, "i");
MODULE_PARM(lpfc28_nodev_tmo, "i");
MODULE_PARM(lpfc28_delay_rsp_err, "i");
MODULE_PARM(lpfc28_nodev_holdio, "i");
MODULE_PARM(lpfc28_check_cond_err, "i");
MODULE_PARM(lpfc28_topology, "i");
MODULE_PARM(lpfc28_link_speed, "i");
MODULE_PARM(lpfc28_fcp_class, "i");
MODULE_PARM(lpfc28_use_adisc, "i");
MODULE_PARM(lpfc28_extra_io_tmo, "i");
MODULE_PARM(lpfc28_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc28_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc28_ack0, "i");
MODULE_PARM(lpfc28_automap, "i");
MODULE_PARM(lpfc28_fcp_bind_method, "i");
MODULE_PARM(lpfc28_cr_delay, "i");
MODULE_PARM(lpfc28_cr_count, "i");
MODULE_PARM(lpfc28_fdmi_on, "i");
MODULE_PARM(lpfc28_max_lun, "i");
MODULE_PARM(lpfc28_discovery_threads, "i");
MODULE_PARM(lpfc28_max_target, "i");
MODULE_PARM(lpfc28_scsi_req_tmo, "i");
MODULE_PARM(lpfc28_lun_skip, "i");

MODULE_PARM(lpfc29_log_verbose, "i");
MODULE_PARM(lpfc29_lun_queue_depth, "i");
MODULE_PARM(lpfc29_tgt_queue_depth, "i");
MODULE_PARM(lpfc29_no_device_delay, "i");
MODULE_PARM(lpfc29_xmt_que_size, "i");
MODULE_PARM(lpfc29_scan_down, "i");
MODULE_PARM(lpfc29_linkdown_tmo, "i");
MODULE_PARM(lpfc29_nodev_tmo, "i");
MODULE_PARM(lpfc29_delay_rsp_err, "i");
MODULE_PARM(lpfc29_nodev_holdio, "i");
MODULE_PARM(lpfc29_check_cond_err, "i");
MODULE_PARM(lpfc29_topology, "i");
MODULE_PARM(lpfc29_link_speed, "i");
MODULE_PARM(lpfc29_fcp_class, "i");
MODULE_PARM(lpfc29_use_adisc, "i");
MODULE_PARM(lpfc29_extra_io_tmo, "i");
MODULE_PARM(lpfc29_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc29_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc29_ack0, "i");
MODULE_PARM(lpfc29_automap, "i");
MODULE_PARM(lpfc29_fcp_bind_method, "i");
MODULE_PARM(lpfc29_cr_delay, "i");
MODULE_PARM(lpfc29_cr_count, "i");
MODULE_PARM(lpfc29_fdmi_on, "i");
MODULE_PARM(lpfc29_max_lun, "i");
MODULE_PARM(lpfc29_discovery_threads, "i");
MODULE_PARM(lpfc29_max_target, "i");
MODULE_PARM(lpfc29_scsi_req_tmo, "i");
MODULE_PARM(lpfc29_lun_skip, "i");

MODULE_PARM(lpfc30_log_verbose, "i");
MODULE_PARM(lpfc30_lun_queue_depth, "i");
MODULE_PARM(lpfc30_tgt_queue_depth, "i");
MODULE_PARM(lpfc30_no_device_delay, "i");
MODULE_PARM(lpfc30_xmt_que_size, "i");
MODULE_PARM(lpfc30_scan_down, "i");
MODULE_PARM(lpfc30_linkdown_tmo, "i");
MODULE_PARM(lpfc30_nodev_tmo, "i");
MODULE_PARM(lpfc30_delay_rsp_err, "i");
MODULE_PARM(lpfc30_nodev_holdio, "i");
MODULE_PARM(lpfc30_check_cond_err, "i");
MODULE_PARM(lpfc30_topology, "i");
MODULE_PARM(lpfc30_link_speed, "i");
MODULE_PARM(lpfc30_fcp_class, "i");
MODULE_PARM(lpfc30_use_adisc, "i");
MODULE_PARM(lpfc30_extra_io_tmo, "i");
MODULE_PARM(lpfc30_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc30_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc30_ack0, "i");
MODULE_PARM(lpfc30_automap, "i");
MODULE_PARM(lpfc30_fcp_bind_method, "i");
MODULE_PARM(lpfc30_cr_delay, "i");
MODULE_PARM(lpfc30_cr_count, "i");
MODULE_PARM(lpfc30_fdmi_on, "i");
MODULE_PARM(lpfc30_max_lun, "i");
MODULE_PARM(lpfc30_discovery_threads, "i");
MODULE_PARM(lpfc30_max_target, "i");
MODULE_PARM(lpfc30_scsi_req_tmo, "i");
MODULE_PARM(lpfc30_lun_skip, "i");

MODULE_PARM(lpfc31_log_verbose, "i");
MODULE_PARM(lpfc31_lun_queue_depth, "i");
MODULE_PARM(lpfc31_tgt_queue_depth, "i");
MODULE_PARM(lpfc31_no_device_delay, "i");
MODULE_PARM(lpfc31_xmt_que_size, "i");
MODULE_PARM(lpfc31_scan_down, "i");
MODULE_PARM(lpfc31_linkdown_tmo, "i");
MODULE_PARM(lpfc31_nodev_tmo, "i");
MODULE_PARM(lpfc31_delay_rsp_err, "i");
MODULE_PARM(lpfc31_nodev_holdio, "i");
MODULE_PARM(lpfc31_check_cond_err, "i");
MODULE_PARM(lpfc31_topology, "i");
MODULE_PARM(lpfc31_link_speed, "i");
MODULE_PARM(lpfc31_fcp_class, "i");
MODULE_PARM(lpfc31_use_adisc, "i");
MODULE_PARM(lpfc31_extra_io_tmo, "i");
MODULE_PARM(lpfc31_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc31_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc31_ack0, "i");
MODULE_PARM(lpfc31_automap, "i");
MODULE_PARM(lpfc31_fcp_bind_method, "i");
MODULE_PARM(lpfc31_cr_delay, "i");
MODULE_PARM(lpfc31_cr_count, "i");
MODULE_PARM(lpfc31_fdmi_on, "i");
MODULE_PARM(lpfc31_max_lun, "i");
MODULE_PARM(lpfc31_discovery_threads, "i");
MODULE_PARM(lpfc31_max_target, "i");
MODULE_PARM(lpfc31_scsi_req_tmo, "i");
MODULE_PARM(lpfc31_lun_skip, "i");

MODULE_PARM(lpfc_log_verbose, "i");
MODULE_PARM(lpfc_lun_queue_depth, "i");
MODULE_PARM(lpfc_tgt_queue_depth, "i");
MODULE_PARM(lpfc_no_device_delay, "i");
MODULE_PARM(lpfc_xmt_que_size, "i");
MODULE_PARM(lpfc_scan_down, "i");
MODULE_PARM(lpfc_linkdown_tmo, "i");
MODULE_PARM(lpfc_nodev_tmo, "i");
MODULE_PARM(lpfc_delay_rsp_err, "i");
MODULE_PARM(lpfc_nodev_holdio, "i");
MODULE_PARM(lpfc_check_cond_err, "i");
MODULE_PARM(lpfc_topology, "i");
MODULE_PARM(lpfc_link_speed, "i");
MODULE_PARM(lpfc_fcp_class, "i");
MODULE_PARM(lpfc_use_adisc, "i");
MODULE_PARM(lpfc_extra_io_tmo, "i");
MODULE_PARM(lpfc_dqfull_throttle_up_time, "i");
MODULE_PARM(lpfc_dqfull_throttle_up_inc, "i");
MODULE_PARM(lpfc_ack0, "i");
MODULE_PARM(lpfc_automap, "i");
MODULE_PARM(lpfc_fcp_bind_method, "i");
MODULE_PARM(lpfc_cr_delay, "i");
MODULE_PARM(lpfc_cr_count, "i");
MODULE_PARM(lpfc_fdmi_on, "i");
MODULE_PARM(lpfc_max_lun, "i");
MODULE_PARM(lpfc_discovery_threads, "i");
MODULE_PARM(lpfc_scsi_req_tmo, "i");
MODULE_PARM(lpfc_max_target, "i");
MODULE_PARM(lpfc_fcp_bind_WWPN, "1-" __MODULE_STRING(MAX_FC_BINDINGS) "s");
MODULE_PARM(lpfc_fcp_bind_WWNN, "1-" __MODULE_STRING(MAX_FC_BINDINGS) "s");
MODULE_PARM(lpfc_fcp_bind_DID, "1-" __MODULE_STRING(MAX_FC_BINDINGS) "s");
MODULE_PARM(lpfc_lun_skip, "i");
MODULE_PARM(lpfc_inq_pqb_filter, "i");

#endif
