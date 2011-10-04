/*
 *
 *  linux/drivers/s390/misc/z90hardware.c
 *
 *  z90crypt 1.2.1
 *
 *  Copyright (C)  2001, 2003 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug support: Jochen Roehrig (roehrig@de.ibm.com)
 *
 *    Support for S390 Crypto Devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/ioctl.h>
#include "z90crypt.h"
#include "z90common.h"
#include <linux/stddef.h>
#include <asm/uaccess.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/list.h>

#define UINT unsigned int
#define USHORT unsigned short
#define UCHAR unsigned char
#define VERSION_Z90HARDWARE_C "$Revision: 1.7.6.7 $"
static const char version[] =
       "z90crypt.o: z90hardware.o ("
       "z90hardware.c " VERSION_Z90HARDWARE_C "/"
       "z90common.h "   VERSION_Z90COMMON_H   "/"
       "z90crypt.h "    VERSION_Z90CRYPT_H    ")";
typedef struct z90c_cca_token_hdr {
  UCHAR z90c_cca_tkn_hdr_id;
  UCHAR z90c_cca_tkn_hdr_ver;
  HWRD  z90c_cca_tkn_length;
  UCHAR z90c_cca_tkn_hdr_rsvq[4];
} Z90C_CCA_TOKEN_HDR;
#define Z90C_CCA_TKN_HDR_ID_EXT 0x1e
#define Z90C_CCA_TKN_HDR_VER_NR 0x00
typedef struct z90c_cca_private_ext_ME_sec {
  UCHAR z90c_cca_pvt_ext_ME_sec_id;
  UCHAR z90c_cca_pvt_ext_ME_sec_ver;
  HWRD  z90c_cca_pvt_ext_ME_sec_length;
  UCHAR z90c_cca_pvt_ext_ME_sec_hash1[20];
  UCHAR z90c_cca_pvt_ext_ME_sec_rsv1[4];
  UCHAR z90c_cca_pvt_ext_ME_sec_fmt;
  UCHAR z90c_cca_pvt_ext_ME_sec_rsv2;
  UCHAR z90c_cca_pvt_ext_ME_sec_hash2[20];
  UCHAR z90c_cca_pvt_ext_ME_sec_usage;
  UCHAR z90c_cca_pvt_ext_ME_sec_rsv3[33];
  UCHAR z90c_cca_pvt_ext_ME_sec_cfdr[24];
  UCHAR z90c_cca_pvt_ext_ME_sec_pvt_exp[128];
  UCHAR z90c_cca_pvt_ext_ME_sec_mod[128];
} Z90C_CCA_PRIVATE_EXT_ME_SEC;
#define Z90C_CCA_PVT_EXT_ME_SEC_ID_PVT 0x02
#define Z90C_CCA_PVT_EXT_ME_SEC_VER_NR 0x00
#define Z90C_CCA_PVT_EXT_ME_SEC_FMT_CL 0x00     
#define Z90C_CCA_PVT_EXT_ME_SEC_FMT_CP 0x82     
#define Z90C_CCA_PVT_USAGE_ALL 0x80
#define Z90C_CCA_PVT_USAGE_NONE 0x40
#define Z90C_CCA_PVT_USAGE_KMGT 0xc0
#define Z90C_CCA_PVT_USAGE_SIGN 0x00
typedef struct z90c_cca_public_sec {
  UCHAR z90c_cca_pub_sec_id;
  UCHAR z90c_cca_pub_sec_ver;
  HWRD  z90c_cca_pub_sec_length;
  UCHAR z90c_cca_pub_sec_rsv1[2];
  HWRD  z90c_cca_pub_sec_exp_len;
  HWRD  z90c_cca_pub_sec_mod_bit_len;
  HWRD  z90c_cca_pub_sec_mod_byte_len; 
  UCHAR z90c_cca_pub_sec_expmod[3];    
} Z90C_CCA_PUBLIC_SEC;
#define Z90C_CCA_PUB_SEC_ID_PVT 0x04
#define Z90C_CCA_PUB_SEC_VER_NR 0x00
typedef struct z90c_cca_private_ext_ME {
  struct z90c_cca_token_hdr pvtMEHdr;
  struct z90c_cca_private_ext_ME_sec pvtMESec;
  struct z90c_cca_public_sec pubMESec;
} Z90C_CCA_PRIVATE_EXT_ME;
typedef struct z90c_cca_public_key {
  struct z90c_cca_token_hdr pubHdr;
  struct z90c_cca_public_sec pubSec;
} Z90C_CCA_PUBLIC_KEY;
typedef struct z90c_cca_private_ext_CRT_sec {
  UCHAR z90c_cca_pvt_ext_CRT_sec_id;
  UCHAR z90c_cca_pvt_ext_CRT_sec_ver;
  HWRD  z90c_cca_pvt_ext_CRT_sec_length;
  UCHAR z90c_cca_pvt_ext_CRT_sec_hash1[20];
  UCHAR z90c_cca_pvt_ext_CRT_sec_rsv1[4];
  UCHAR z90c_cca_pvt_ext_CRT_sec_fmt;
  UCHAR z90c_cca_pvt_ext_CRT_sec_rsv2;
  UCHAR z90c_cca_pvt_ext_CRT_sec_hash2[20];
  UCHAR z90c_cca_pvt_ext_CRT_sec_usage;
  UCHAR z90c_cca_pvt_ext_CRT_sec_rsv3[3];
  HWRD  z90c_cca_pvt_ext_CRT_sec_p_len;
  HWRD  z90c_cca_pvt_ext_CRT_sec_q_len;
  HWRD  z90c_cca_pvt_ext_CRT_sec_dp_len;
  HWRD  z90c_cca_pvt_ext_CRT_sec_dq_len;
  HWRD  z90c_cca_pvt_ext_CRT_sec_u_len;
  HWRD  z90c_cca_pvt_ext_CRT_sec_mod_len;
  UCHAR z90c_cca_pvt_ext_CRT_sec_rsv4[4];
  HWRD  z90c_cca_pvt_ext_CRT_sec_pad_len;
  UCHAR z90c_cca_pvt_ext_CRT_sec_rsv5[52];
  UCHAR z90c_cca_pvt_ext_CRT_sec_cfdr[8];        
} Z90C_CCA_PRIVATE_EXT_CRT_SEC;
#define Z90C_CCA_PVT_EXT_CRT_SEC_ID_PVT 0x08
#define Z90C_CCA_PVT_EXT_CRT_SEC_VER_NR 0x00
#define Z90C_CCA_PVT_EXT_CRT_SEC_FMT_CL 0x40     
#define Z90C_CCA_PVT_EXT_CRT_SEC_FMT_CP 0x42     
typedef struct z90c_cca_private_ext_CRT {
  struct z90c_cca_token_hdr pvtCrtHdr;
  struct z90c_cca_private_ext_CRT_sec pvtCrtSec;
  struct z90c_cca_public_sec pubCrtSec;
} Z90C_CCA_PRIVATE_EXT_CRT;
#ifndef Z90C_PCICC_RESET
#define Z90C_PCICC_RESET 45        
#endif
#ifndef Z90C_LITE_RESET
#define Z90C_LITE_RESET 45          
#endif
typedef struct ap_status_word {
  UCHAR ap_q_stat_flags;
  UCHAR ap_response_code;
  UCHAR ap_reserved[2];
} AP_STATUS_WORD;
#define AP_Q_STATUS_EMPTY           0x80
#define AP_Q_STATUS_REPLIES_WAITING 0x40
#define AP_Q_STATUS_ARRAY_FULL      0x20
#define AP_RESPONSE_NORMAL            0x00
#define AP_RESPONSE_Q_NOT_AVAIL       0x01
#define AP_RESPONSE_RESET_IN_PROGRESS 0x02
#define AP_RESPONSE_DECONFIGURED      0x03
#define AP_RESPONSE_CHECKSTOPPED      0x04
#define AP_RESPONSE_BUSY              0x05
#define AP_RESPONSE_Q_FULL            0x10
#define AP_RESPONSE_NO_PENDING_REPLY  0x10   
#define AP_RESPONSE_INDEX_TOO_BIG     0x11
#define AP_RESPONSE_NO_FIRST_PART     0x13
#define AP_RESPONSE_MESSAGE_TOO_BIG   0x15
#define AP_MAX_CDX_BITL 4
#define AP_RQID_RESERVED_BITL 4
typedef struct z90c_type4_hdr {
  BYTE z90c_type4_rsv1;          
  UCHAR z90c_type4_code;         
  HWRD z90c_type4_m_len;         
  UCHAR z90c_type4_req_code;     
  UCHAR z90c_type4_m_fmt;        
  HWRD z90c_type4_rsv2;          
} Z90C_TYPE4_HDR;
#define Z90C_TYPE4_TYPE_CODE 0x04
#define Z90C_TYPE4_REQU_CODE 0x40
#define Z90C_TYPE4_SME_LEN 0x0188
#define Z90C_TYPE4_LME_LEN 0x0308
#define Z90C_TYPE4_SCR_LEN 0x01E0
#define Z90C_TYPE4_LCR_LEN 0x03A0
#define Z90C_TYPE4_SME_FMT 0x00
#define Z90C_TYPE4_LME_FMT 0x10
#define Z90C_TYPE4_SCR_FMT 0x40
#define Z90C_TYPE4_LCR_FMT 0x50
typedef struct z90c_type4_sme {     
  Z90C_TYPE4_HDR z90c_type4_sme_hdr;
  UCHAR z90c_type4_sme_msg[128];    
  UCHAR z90c_type4_sme_exp[128];    
  UCHAR z90c_type4_sme_mod[128];    
} Z90C_TYPE4_SME;
typedef struct z90c_type4_lme {     
  Z90C_TYPE4_HDR z90c_type4_lme_hdr;
  UCHAR z90c_type4_lme_msg[256];    
  UCHAR z90c_type4_lme_exp[256];    
  UCHAR z90c_type4_lme_mod[256];    
} Z90C_TYPE4_LME;
typedef struct z90c_type4_scr {     
  Z90C_TYPE4_HDR z90c_type4_scr_hdr;
  UCHAR z90c_type4_scr_msg[128];    
  UCHAR z90c_type4_scr_dp[72];      
  UCHAR z90c_type4_scr_dq[64];      
  UCHAR z90c_type4_scr_p[72];       
  UCHAR z90c_type4_scr_q[64];       
  UCHAR z90c_type4_scr_u[72];       
} Z90C_TYPE4_SCR;
typedef struct z90c_type4_lcr {     
  Z90C_TYPE4_HDR z90c_type4_lcr_hdr;
  UCHAR z90c_type4_lcr_msg[256];    
  UCHAR z90c_type4_lcr_dp[136];     
  UCHAR z90c_type4_lcr_dq[128];     
  UCHAR z90c_type4_lcr_p[136];      
  UCHAR z90c_type4_lcr_q[128];      
  UCHAR z90c_type4_lcr_u[136];      
} Z90C_TYPE4_LCR;
typedef union z90c_type4_msg {
  struct z90c_type4_sme sme;
  struct z90c_type4_lme lme;
  struct z90c_type4_scr scr;
  struct z90c_type4_lcr lcr;
} Z90C_TYPE4_MSG;
typedef struct z90c_type84_hdr {
  BYTE z90c_type84_rsv1;         
  UCHAR z90c_type84_code;        
  HWRD z90c_type84_len;          
  BYTE z90c_type84_rsv2[4];      
} Z90C_TYPE84_HDR;
#define Z90C_TYPE84_RSP_CODE 0x84
#define Z90C_TYPE84_SMALL_LEN 0x0088
#define Z90C_TYPE84_LARGE_LEN 0x0108
typedef struct z90c_type84_small {  
  Z90C_TYPE84_HDR hdr;              
  UCHAR response[128];              
} Z90C_TYPE84_SMALL;
typedef struct z90c_type84_large {  
  Z90C_TYPE84_HDR hdr;              
  UCHAR response[256];              
} Z90C_TYPE84_LARGE;
typedef union z90c_type84_msg {
  struct z90c_type84_small small;
  struct z90c_type84_large large;
} Z90C_TYPE84_MSG;
typedef struct z90c_type6_hdr {
  UCHAR z90c_type6_hdr_rsv1;         
  UCHAR z90c_type6_hdr_type;         
  UCHAR z90c_type6_hdr_rsv2[2];      
  UCHAR z90c_type6_hdr_right[4];     
  UCHAR z90c_type6_hdr_rsv3[2];      
  UCHAR z90c_type6_hdr_rsv4[2];      
  UCHAR z90c_type6_hdr_apfs[4];      
  UINT  z90c_type6_hdr_offs1;        
  UINT  z90c_type6_hdr_offs2;        
  UINT  z90c_type6_hdr_offs3;        
  UINT  z90c_type6_hdr_offs4;        
  UCHAR z90c_type6_scc_agent_id[16]; 
  UCHAR z90c_type6_scc_rqid[2];      
  UCHAR z90c_type6_hdr_rsv5[2];      
  UCHAR z90c_type6_scc_function[2];  
  UCHAR z90c_type6_hdr_rsv6[2];      
  UINT  z90c_type6_hdr_ToCardLen1;   
  UINT  z90c_type6_hdr_ToCardLen2;   
  UINT  z90c_type6_hdr_ToCardLen3;   
  UINT  z90c_type6_hdr_ToCardLen4;   
  UINT  z90c_type6_hdr_FrCardLen1;   
  UINT  z90c_type6_hdr_FrCardLen2;   
  UINT  z90c_type6_hdr_FrCardLen3;   
  UINT  z90c_type6_hdr_FrCardLen4;   
} Z90C_TYPE6_HDR;
typedef struct z90c_cprb {
   UCHAR           cprb_len[2];     
   UCHAR           cprb_ver_id;     
   UCHAR           pad_000;         
   UCHAR           srpi_rtcode[4];  
   UCHAR           srpi_verb;       
   UCHAR           flags;           
   UCHAR           func_id[2];      
   UCHAR           checkpoint_flag; 
   UCHAR           resv2;           
   UCHAR           req_parml[2];    
   UCHAR           req_parmp[4];    
   UCHAR           req_datal[4];    
   UCHAR           req_datap[4];    
   UCHAR           rpl_parml[2];    
   UCHAR           pad_001[2];      
   UCHAR           rpl_parmp[4];    
   UCHAR           rpl_datal[4];    
   UCHAR           rpl_datap[4];    
   UCHAR           ccp_rscode[2];   
   UCHAR           ccp_rtcode[2];   
   UCHAR           repd_parml[2];   
   UCHAR           mac_data_len[2]; 
   UCHAR           repd_datal[4];   
   UCHAR           req_pc[2];       
   UCHAR           res_origin[8];   
   UCHAR           mac_value[8];    
   UCHAR           logon_id[8];     
   UCHAR           usage_domain[2]; 
   UCHAR           resv3[18];       
   UCHAR           svr_namel[2];    
   UCHAR           svr_name[8];     
}Z90C_CPRB;
#pragma pack(1)
typedef struct z90c_cprbx{
   USHORT          cprb_len;        
   UCHAR           cprb_ver_id;     
   UCHAR           pad_000[3];      
   UCHAR           func_id[2];      
   UCHAR           cprb_flags[4];   
   UINT            req_parml;       
   UINT            req_datal;       
   UINT            rpl_msgbl;       
   UINT            rpld_parml;      
   UINT            rpl_datal;       
   UINT            rpld_datal;      
   UINT            req_extbl;       
   UCHAR           pad_001[4];      
   UINT            rpld_extbl;      
   UCHAR           req_parmb[16];   
   UCHAR           req_datab[16];   
   UCHAR           rpl_parmb[16];   
   UCHAR           rpl_datab[16];   
   UCHAR           req_extb[16];    
   UCHAR           rpl_extb[16];    
   USHORT          ccp_rtcode;      
   USHORT          ccp_rscode;      
   UINT            mac_data_len;    
   UCHAR           logon_id[8];     
   UCHAR           mac_value[8];    
   UCHAR           mac_content_flgs;
   UCHAR           pad_002;         
   USHORT          domain;          
   UCHAR           pad_003[12];     
   UCHAR           pad_004[36];     
}Z90C_CPRBX;
#pragma pack()
#define REPLY_ERROR_MACHINE_FAILURE  0x10
#define REPLY_ERROR_PREEMPT_FAILURE  0x12
#define REPLY_ERROR_CHECKPT_FAILURE  0x14
#define REPLY_ERROR_MESSAGE_TYPE     0x20
#define REPLY_ERROR_INVALID_COMM_CD  0x21  
#define REPLY_ERROR_INVALID_MSG_LEN  0x23
#define REPLY_ERROR_RESERVD_FIELD    0x24  
typedef struct rule_block {
  UCHAR            rule_array_len[2];
  UCHAR            rule_array[0];   
} RULE_BLOCK;
typedef struct vud_block {
  UCHAR            vud_len[2];      
  UCHAR            vud[0];          
} VUD_BLOCK;
typedef struct key_desc {
  UCHAR            key_len[2];      
  UCHAR            key_flag[2];     
  UCHAR            key[0];          
} KEY_DESC;
typedef struct key_block {
  UCHAR            keys_len[2];     
  struct key_desc keys[0];          
} KEY_BLOCK;
typedef struct parm_block {
  UCHAR            function_code[2];
  struct rule_block rule_array;     
  struct vud_block vud;             
  struct key_block keys;            
} PARM_BLOCK;
#define CPRB_UDX 0x80               
typedef struct z90c_type6_msg {
  Z90C_TYPE6_HDR z90c_type6_hdr;
  Z90C_CPRB z90c_pkd_cprb;
} Z90C_TYPE6_MSG;
#define Z90C_TYPE6_TYPE_CODE 0x06
typedef union z90c_request_msg {
  union z90c_type4_msg t4msg;
  struct z90c_type6_msg t6msg;
} Z90C_REQUEST_MSG;
typedef struct z90c_request_msg_ext {
  int q_nr;
  UCHAR * psmid;
  union z90c_request_msg reqMsg;
} Z90C_REQUEST_MSG_EXT;
typedef struct z90c_type82_hdr {
  UCHAR z90c_type82_hdr_rsv1;        
  UCHAR z90c_type82_hdr_type;        
  UCHAR z90c_type82_hdr_rsv2[2];     
  UCHAR z90c_type82_hdr_reply;       
  UCHAR z90c_type82_hdr_rsv3[3];     
} Z90C_TYPE82_HDR;
#define Z90C_TYPE82_RSP_CODE 0x82
#define REPLY_ERROR_MACHINE_FAILURE  0x10
#define REPLY_ERROR_PREEMPT_FAILURE  0x12
#define REPLY_ERROR_CHECKPT_FAILURE  0x14
#define REPLY_ERROR_MESSAGE_TYPE     0x20
#define REPLY_ERROR_INVALID_COMM_CD  0x21  
#define REPLY_ERROR_INVALID_MSG_LEN  0x23
#define REPLY_ERROR_RESERVD_FIELD    0x24  
#define REPLY_ERROR_FORMAT_FIELD     0x29
#define REPLY_ERROR_INVALID_COMMAND  0x30  
#define REPLY_ERROR_MALFORMED_MSG    0x40
#define REPLY_ERROR_RESERVED_FIELD   0x50  
#define REPLY_ERROR_WORD_ALIGNMENT   0x60
#define REPLY_ERROR_MESSAGE_LENGTH   0x80
#define REPLY_ERROR_OPERAND_INVALID  0x82
#define REPLY_ERROR_OPERAND_SIZE     0x84
#define REPLY_ERROR_EVEN_MOD_IN_OPND 0x85
#define REPLY_ERROR_TRANSPORT_FAIL   0x90
#define REPLY_ERROR_PACKET_TRUNCATED 0xA0
#define REPLY_ERROR_ZERO_BUFFER_LEN  0xB0
typedef struct z90c_type86_hdr {
  UCHAR z90c_type86_hdr_rsv1;         
  UCHAR z90c_type86_hdr_type;         
  UCHAR z90c_type86_hdr_fmt ;         
  UCHAR z90c_type86_hdr_rsv2;         
  UCHAR z90c_type86_hdr_reply;        
  UCHAR z90c_type86_hdr_rsv3[3];      
} Z90C_TYPE86_HDR;
#define Z90C_TYPE86_RSP_CODE 0x86
#define Z90C_TYPE86_FMT1     0x01
#define Z90C_TYPE86_FMT2     0x02
typedef struct z90c_type86_fmt2_msg {
  struct z90c_type86_hdr hdr;
  UCHAR z90c_type86_hdr_rsv4[4];      
  UCHAR z90c_type86_hdr_apfs[4];      
  UINT  z90c_type86_hdr_coun1;        
  UINT  z90c_type86_hdr_offs1;        
  UINT  z90c_type86_hdr_coun2;        
  UINT  z90c_type86_hdr_offs2;        
  UINT  z90c_type86_hdr_coun3;        
  UINT  z90c_type86_hdr_offs3;        
  UINT  z90c_type86_hdr_coun4;        
  UINT  z90c_type86_hdr_offs4;        
} Z90C_TYPE86_FMT2_MSG;
typedef union z90c_resp_hdr {
  struct z90c_type82_hdr t82;
  struct z90c_type84_hdr t84;
  struct z90c_type86_hdr t86;
} Z90C_RESP_HDR;
static Z90C_TYPE6_HDR stat_type6_hdr = {
  0x00,                             
  0x06,                             
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  0x00000058,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  {0x01,0x00,0x43,0x43,0x41,0x2D,0x41,0x50,
   0x50,0x4C,0x20,0x20,0x20,0x01,0x01,0x01}, 
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x50,0x44},                      
  {0x00,0x00},                      
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000                        
};
static Z90C_TYPE6_HDR stat_type6_hdrX = {
  0x00,                             
  0x06,                             
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  0x00000058,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  {0x43,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x50,0x44},                      
  {0x00,0x00},                      
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000                        
};
static Z90C_CPRB stat_cprb = {
  {0x70,0x00},                      
  0x41,                             
  0x00,                             
  {0x00,0x00,0x00,0x00},            
  0x00,                             
  0x00,                             
  {0x54,0x32},                      
  0x01,                             
  0x00,                             
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00},            
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  
  {0x00,0x00},                      
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00},                      
  {0x08,0x00},                      
  {0x49,0x43, 0x53,0x46,0x20,0x20,0x20,0x20} 
  };
struct function_and_rules_block {
  UCHAR fc[2];
  UCHAR ulen[2];
  UCHAR only_rule[8];
};
static struct function_and_rules_block stat_pkd_function_and_rules= {
  {0x50,0x44},                       
  {0x0A,0x00},                       
  {'P','K','C','S','-','1','.','2'}
} ;
static struct function_and_rules_block stat_pke_function_and_rules= {
  {0x50,0x4B},                       
  {0x0A,0x00},                       
  {'P','K','C','S','-','1','.','2'}
} ;
struct pkd_T6_keyBlock_hdr {
  UCHAR blen[2];
  UCHAR ulen[2];
  UCHAR flags[2];
};
struct pkd_T6_keyBlock_hdrX {
  USHORT blen;
  USHORT ulen;
  UCHAR flags[2];
};
static struct pkd_T6_keyBlock_hdr stat_pkd_T6_keyBlock_hdr = {
  {0x89,0x01},       
  {0x87,0x01},       
  {0x00}
} ;
static Z90C_CPRBX stat_cprbx = {
  0x00DC,                           
  0x02,                             
  {0x00,0x00,0x00},                 
  {0x54,0x32},                      
  {0x00,0x00,0x00,0x00},            
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  0x00000000,                       
  {0x00,0x00,0x00,0x00},            
  0x00000000,                       
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  0x0000,                           
  0x0000,                           
  0x00000000,                       
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  0x00,                             
  0x00,                             
  0x0000,                           
  {0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00},            
  {0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00}   
};
static struct function_and_rules_block stat_pkd_function_and_rulesX= {
  {0x50,0x44},                       
  {0x00,0x0A},                       
  {'P','K','C','S','-','1','.','2'}
} ;
static struct function_and_rules_block stat_pke_function_and_rulesX= {
  {0x50,0x4B},                       
  {0x00,0x0A},                       
  {'Z','E','R','O','-','P','A','D'}
} ;
static struct pkd_T6_keyBlock_hdrX stat_pkd_T6_keyBlock_hdrX = {
  0x0189,            
  0x0187,            
  {0x00}
} ;
static unsigned char stat_pad[256] = {
0x1b,0x7b,0x5d,0xb5,0x75,0x01,0x3d,0xfd,
0x8d,0xd1,0xc7,0x03,0x2d,0x09,0x23,0x57,
0x89,0x49,0xb9,0x3f,0xbb,0x99,0x41,0x5b,
0x75,0x21,0x7b,0x9d,0x3b,0x6b,0x51,0x39,
0xbb,0x0d,0x35,0xb9,0x89,0x0f,0x93,0xa5,
0x0b,0x47,0xf1,0xd3,0xbb,0xcb,0xf1,0x9d,
0x23,0x73,0x71,0xff,0xf3,0xf5,0x45,0xfb,
0x61,0x29,0x23,0xfd,0xf1,0x29,0x3f,0x7f,
0x17,0xb7,0x1b,0xa9,0x19,0xbd,0x57,0xa9,
0xd7,0x95,0xa3,0xcb,0xed,0x1d,0xdb,0x45,
0x7d,0x11,0xd1,0x51,0x1b,0xed,0x71,0xe9,
0xb1,0xd1,0xab,0xab,0x21,0x2b,0x1b,0x9f,
0x3b,0x9f,0xf7,0xf7,0xbd,0x63,0xeb,0xad,
0xdf,0xb3,0x6f,0x5b,0xdb,0x8d,0xa9,0x5d,
0xe3,0x7d,0x77,0x49,0x47,0xf5,0xa7,0xfd,
0xab,0x2f,0x27,0x35,0x77,0xd3,0x49,0xc9,
0x09,0xeb,0xb1,0xf9,0xbf,0x4b,0xcb,0x2b,
0xeb,0xeb,0x05,0xff,0x7d,0xc7,0x91,0x8b,
0x09,0x83,0xb9,0xb9,0x69,0x33,0x39,0x6b,
0x79,0x75,0x19,0xbf,0xbb,0x07,0x1d,0xbd,
0x29,0xbf,0x39,0x95,0x93,0x1d,0x35,0xc7,
0xc9,0x4d,0xe5,0x97,0x0b,0x43,0x9b,0xf1,
0x16,0x93,0x03,0x1f,0xa5,0xfb,0xdb,0xf3,
0x27,0x4f,0x27,0x61,0x05,0x1f,0xb9,0x23,
0x2f,0xc3,0x81,0xa9,0x23,0x71,0x55,0x55,
0xeb,0xed,0x41,0xe5,0xf3,0x11,0xf1,0x43,
0x69,0x03,0xbd,0x0b,0x37,0x0f,0x51,0x8f,
0x0b,0xb5,0x89,0x5b,0x67,0xa9,0xd9,0x4f,
0x01,0xf9,0x21,0x77,0x37,0x73,0x79,0xc5,
0x7f,0x51,0xc1,0xcf,0x97,0xa1,0x75,0xad,
0x35,0x9d,0xd3,0xd3,0xa7,0x9d,0x5d,0x41,
0x6f,0x65,0x1b,0xcf,0xa9,0x87,0x91,0x09
};
static Z90C_CCA_PRIVATE_EXT_ME stat_pvt_me_key = {
  {                               
    0x1E,                           
    0x00,                           
    0x0183,                         
    {0x00,0x00,0x00,0x00}           
  },                              
  {                               
    0x02,                           
    0x00,                           
    0x016C,                         
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00},  
    {0x00,0x00,0x00,0x00},          
    0x00,                           
    0x00,                           
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00},  
    0x80,                           
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  
  },                              
  {                               
    0x04,                           
    0x00,                           
    0x000F,                         
    {0x00,0x00},                    
    0x0003,                         
    0x0000,                         
    0x0000,                         
    {0x01,0x00,0x01}                
  }                               
};
static Z90C_CCA_PUBLIC_KEY stat_pub_key = {
  {                               
    0x1E,                           
    0x00,                           
    0x0000,                         
    {0x00,0x00,0x00,0x00}           
  },                              
  {                               
    0x04,                           
    0x00,                           
    0x0000,                         
    {0x00,0x00},                    
    0x0000,                         
    0x0000,                         
    0x0000,                         
    {0x01,0x00,0x01}                
  }                               
};
#define FIXED_TYPE6_ME_LEN 0x0000025F 
#define FIXED_TYPE6_ME_EN_LEN 0x000000F0 
#define FIXED_TYPE6_ME_LENX 0x000002CB 
#define FIXED_TYPE6_ME_EN_LENX 0x0000015C 
static Z90C_CCA_PUBLIC_SEC stat_cca_public_sec =
{                               
  0x04,                           
  0x00,                           
  0x000f,                         
  {0x00,0x00},                    
  0x0003,                         
  0x0000,                         
  0x0000,                         
  {0x01,0x00,0x01}                
};                              
#define FIXED_TYPE6_CR_LEN 0x00000177 
#define FIXED_TYPE6_CR_LENX 0x000001E3 
#ifndef MAX_RESPONSE_SIZE
#define MAX_RESPONSE_SIZE 0x00000710
#define MAX_RESPONSEX_SIZE 0x0000077C
#endif
#define RESPONSE_CPRB_SIZE 0x000005B8 
#define RESPONSE_CPRBX_SIZE 0x00000724 
static UCHAR stat_PE_function_code[2] = {0x50, 0x4B};
static UCHAR testmsg[] = {
0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x00,0x06,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x58,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x43,0x43,
0x41,0x2d,0x41,0x50,0x50,0x4c,0x20,0x20,0x20,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
0x50,0x4b,0x00,0x00,0x00,0x00,0x01,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x05,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x70,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x54,0x32,
0x01,0x00,0xa0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xb8,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x49,0x43,0x53,0x46,
0x20,0x20,0x20,0x20,0x50,0x4b,0x0a,0x00,0x50,0x4b,0x43,0x53,0x2d,0x31,0x2e,0x32,
0x37,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,
0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,
0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,0x55,0x66,
0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x5d,0x00,0x5b,0x00,0x77,0x88,0x1e,0x00,0x00,
0x57,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x4f,0x00,0x00,0x00,0x03,0x02,0x00,0x00,
0x40,0x01,0x00,0x01,0xce,0x02,0x68,0x2d,0x5f,0xa9,0xde,0x0c,0xf6,0xd2,0x7b,0x58,
0x4b,0xf9,0x28,0x68,0x3d,0xb4,0xf4,0xef,0x78,0xd5,0xbe,0x66,0x63,0x42,0xef,0xf8,
0xfd,0xa4,0xf8,0xb0,0x8e,0x29,0xc2,0xc9,0x2e,0xd8,0x45,0xb8,0x53,0x8c,0x6f,0x4e,
0x72,0x8f,0x6c,0x04,0x9c,0x88,0xfc,0x1e,0xc5,0x83,0x55,0x57,0xf7,0xdd,0xfd,0x4f,
0x11,0x36,0x95,0x5d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
  };
static UCHAR testrepl[] = {
0x00,0x86,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0xb8,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x70,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x54,0x32,0x01,0x00,0xa0,0x00,
0x90,0x80,0x09,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xb8,0x05,0x00,0x00,
0x90,0x94,0x09,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  };
inline int testq (int q_nr, int * q_depth, int * dev_type, AP_STATUS_WORD *stat)
{
  int ccode;
#ifdef CONFIG_ARCH_S390X
  asm volatile (" lgr   0,%4 \n"         
		"\t slgr 1,1 \n"         
		"\t lgr  2,1 \n"         
#else
  asm volatile (" lr 0,%4 \n"            
		"\t slr 1,1 \n"          
		"\t lr 2,1 \n"           
#endif
		"0:\t .long 0xb2af0000 \n" 
		"1:\t ipm %0  \n"        
		"\t srl %0,28 \n"        
#ifdef CONFIG_ARCH_S390X
		"\t iihh %0,0 \n"        
		"\t iihl %0,0 \n"        
		"\t lgr  %1,1 \n"        
                "\t lgr  %3,2 \n"
                "\t srl  %3,24 \n"        
		"\t sll  2,24 \n"
		"\t srl  2,24 \n"
		"\t lgr  %2,2 \n"        
#else
		"\t lr %1,1   \n"        
                "\t lr %3,2   \n"        
                "\t srl %3,24 \n"        
		"\t sll 2,24  \n"
		"\t srl 2,24  \n"
		"\t lr %2,2   \n"        
#endif
		"2:       \n"            
		".section .fixup,\"ax\"\n"
		"3:       \n"            
		"\t lhi   %0,%h5\n"
		"\t bras  1,4f\n"
		"\t .long 2b\n"
		"4:             \n"
		"\t l     1,0(1)\n"
		"\t br    1\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
#ifdef CONFIG_ARCH_S390X
		"   .align 8\n"
		"   .quad  0b,3b\n"
		"   .quad  1b,3b\n"
#else
		"   .align 4\n"
		"   .long  0b,3b\n"
		"   .long  1b,3b\n"
#endif
		".previous"
		:"=d" (ccode),"=d" (*stat),"=d" (*q_depth), "=d" (*dev_type)
		:"d" (q_nr), "K" (DEV_TSQ_EXCEPTION)
		:"cc","0","1","2");
  return ccode;
}; 
inline int resetq (int q_nr, AP_STATUS_WORD *stat_p)
{
  int ccode;
#ifdef CONFIG_ARCH_S390X
  asm volatile ("\t lgr 0,%2 \n"         
		"\t lghi 1,1  \n"        
#else
  asm volatile ("\t lr 0,%2 \n"          
		"\t lhi 1,1  \n"         
#endif
		"\t sll 1,24 \n"         
		"\t or  0,1 \n"          
#ifdef CONFIG_ARCH_S390X
		"\t slgr 1,1 \n"         
		"\t lgr 2,1 \n"          
#else
		"\t slr 1,1 \n"          
		"\t lr 2,1 \n"           
#endif
		"0:\t .long 0xb2af0000 \n" 
		"1:\t ipm %0 \n"         
		"\t srl %0,28 \n"        
#ifdef CONFIG_ARCH_S390X
		"\t iihh %0,0 \n"        
		"\t iihl %0,0 \n"        
		"\t lgr %1,1 \n"         
#else
		"\t lr %1,1 \n"          
#endif
		"2:       \n"            
		".section .fixup,\"ax\"\n"
		"3:       \n"            
		"\t lhi   %0,%h3\n"
		"\t bras  1,4f\n"
		"\t .long 2b\n"
		"4:             \n"
		"\t l     1,0(1)\n"
		"\t br    1\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
#ifdef CONFIG_ARCH_S390X
		"   .align 8\n"
		"   .quad  0b,3b\n"
		"   .quad  1b,3b\n"
#else
		"   .align 4\n"
		"   .long  0b,3b\n"
		"   .long  1b,3b\n"
#endif
		".previous"
		:"=d" (ccode),"=d" (*stat_p)
		:"d" (q_nr), "K" (DEV_RSQ_EXCEPTION)
		:"cc","0","1","2");
  return ccode;
}; 
inline int sen(int msg_len,
	       UCHAR * msg_ext,
	       AP_STATUS_WORD * stat)
{
  int ccode;
#ifdef CONFIG_ARCH_S390X
  asm volatile (" lgr 6,%3 \n"           
		"\t lgr 7,%2 \n"         
		"\t llgt 0,0(6) \n"      
		"\t lghi 1,64 \n"        
#else
  asm volatile (" lr  6,%3 \n"           
		"\t lr  7,%2 \n"         
		"\t l   0,0(6) \n"       
		"\t lhi 1,64 \n"         
#endif
		"\t sll 1,24 \n"         
		"\t or  0,1 \n"          
		"\t la 6,4(6) \n"        
#ifdef CONFIG_ARCH_S390X
		"\t llgt 2,0(6) \n"      
		"\t llgt 3,4(6) \n"      
#else
		"\t l  2,0(6) \n"        
		"\t l  3,4(6) \n"        
#endif
		"\t la 6,8(6) \n"        
		"\t slr 1,1 \n"          
		"0:\t .long 0xb2ad0026 \n" 
		"1:\t brc 2,0b \n"       
		"\t ipm %0 \n"           
		"\t srl %0,28 \n"        
#ifdef CONFIG_ARCH_S390X
		"\t iihh %0,0 \n"        
		"\t iihl %0,0 \n"        
		"\t lgr %1,1 \n"         
#else
		"\t lr %1,1 \n"          
#endif
		"2:       \n"            
		".section .fixup,\"ax\"\n"
		"3:       \n"            
		"\t lhi   %0,%h4\n"
		"\t bras  1,4f\n"
		"\t .long 2b\n"
		"4:             \n"
		"\t l      1,0(1)\n"
		"\t br    1\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
#ifdef CONFIG_ARCH_S390X
		"   .align 8\n"
		"   .quad  0b,3b\n"
		"   .quad  1b,3b\n"
#else
		"   .align 4\n"
		"   .long  0b,3b\n"
		"   .long  1b,3b\n"
#endif
		".previous"
		:"=d" (ccode),"=d" (*stat)
		:"d" (msg_len),"a" (msg_ext), "K" (DEV_SEN_EXCEPTION)
		:"cc","0","1","2","3","6","7");
  return ccode;
}; 
inline int rec(int q_nr,
	       int buff_l,
	       UCHAR * rsp,
	       UCHAR * id,
	       AP_STATUS_WORD * st)
{
  int ccode;
  asm volatile
#ifdef CONFIG_ARCH_S390X
    (" lgr 0,%2   \n"                    
     "\t lgr  3,%4 \n"                   
     "\t lgr  6,%3 \n"                   
     "\t lgr  7,%5 \n"                   
     "\t lghi 1,128 \n"                  
#else
    (" lr 0,%2    \n"                    
     "\t lr  3,%4 \n"                    
     "\t lr  6,%3  \n"                   
     "\t lr  7,%5  \n"                   
     "\t lhi 1,128 \n"                   
#endif
     "\t sll 1,24 \n"                    
     "\t or  0,1  \n"                    
#ifdef CONFIG_ARCH_S390X
     "\t slgr 1,1  \n"                   
     "\t lgr  2,1  \n"                   
     "\t lgr  4,1  \n"                   
     "\t lgr  5,1  \n"                   
#else
     "\t slr 1,1  \n"                    
     "\t lr  2,1  \n"                    
     "\t lr  4,1  \n"                    
     "\t lr  5,1  \n"                    
#endif
     "0:\t .long 0xb2ae0046 \n"          
     "1:\t brc 2,0b \n"                  
     "\t brc 4,0b \n"                    
     "\t ipm %0   \n"                    
     "\t srl %0,28 \n"                   
#ifdef CONFIG_ARCH_S390X
     "\t iihh %0,0 \n"                   
     "\t iihl %0,0 \n"                   
     "\t lgr %1,1 \n"                    
#else
     "\t lr %1,1 \n"                     
#endif
     "\t st  4,0(3) \n"                  
     "\t st  5,4(3) \n"                  
     "2:       \n"                       
     ".section .fixup,\"ax\"\n"
     "3:       \n"                       
     "\t lhi   %0,%h6\n"
     "\t bras  1,4f\n"
     "\t .long 2b\n"
     "4:             \n"
     "\t l     1,0(1)\n"
     "\t br    1\n"
     ".previous\n"
     ".section __ex_table,\"a\"\n"
#ifdef CONFIG_ARCH_S390X
     "   .align 8\n"
     "   .quad  0b,3b\n"
     "   .quad  1b,3b\n"
#else
     "   .align 4\n"
     "   .long  0b,3b\n"
     "   .long  1b,3b\n"
#endif
     ".previous"
     :"=d"(ccode),"=d"(*st)
     :"d" (q_nr), "d" (rsp), "d" (id), "d" (buff_l), "K" (DEV_REC_EXCEPTION)
     :"cc","0","1","2","3","4","5","6","7","memory");
  return ccode;
}; 
inline void itoLe2(int * i_p, UCHAR * lechars)
{
  *lechars       = *((UCHAR *) i_p + sizeof(int) - 1);
  *(lechars + 1) = *((UCHAR *) i_p + sizeof(int) - 2);
} ;
inline void le2toI(UCHAR * lechars, int * i_p)
{
  UCHAR * ic_p;
  *i_p = 0;
  ic_p = (UCHAR *) i_p;
  *(ic_p + 2) = *(lechars + 1);
  *(ic_p + 3) = *(lechars);
} ;
inline int isNotEmpty (unsigned char * ptr, int len)
{
  return (memcmp(ptr,(unsigned char *)&stat_pvt_me_key+60, len));
}; 
int get_test_msg(UCHAR * buffer, UINT * pBuffLen)
{
  memcpy(buffer, testmsg, sizeof(testmsg));
  *pBuffLen = sizeof(testmsg);
  return 0;
} 
int test_reply(UCHAR * buffer)
{
  return (memcmp(buffer, testrepl, 2));
} 
HDSTAT query_on_line (int deviceNr, int cdx, int resetNr, int *q_depth,
                      int *dev_type)
{
  int q_nr;
  DEVSTAT ccode = 0;
  AP_STATUS_WORD stat_word;
  HDSTAT stat;
  BOOL break_out = FALSE;
  int i;
  int t_depth;
  int t_dev_type;
  q_nr =(deviceNr << (AP_MAX_CDX_BITL + AP_RQID_RESERVED_BITL)) + cdx;
  stat = HD_BUSY;
  ccode = testq(q_nr,&t_depth,&t_dev_type,&stat_word);
  PDEBUG("query_on_line: testq returned ccode %d, response_code %d\n",
         ccode, stat_word.ap_response_code);
  for (i=0;i<resetNr;i++){
    if (ccode > 3){                   
      stat = (HD_TSQ_EXCEPTION);
      PRINTKC("query_on_line: Exception testing device %i\n",i);
      break;
    }
    switch(ccode) {
      case(0):
        PDEBUG("query_on_line: testq returned t_dev_type %d\n", t_dev_type);
	break_out = TRUE;
        stat = HD_ONLINE;
	*q_depth = t_depth + 1;
        switch (t_dev_type) {
          case OTHER_HW:
          case OTHER2_HW:
            stat = HD_NOT_THERE;
            *dev_type = NILDEV;
            break;
          case PCICA_HW:
            *dev_type = PCICA;
            break;
          case PCICC_HW:
            *dev_type = PCICC;
            break;
          case PCIXCC_HW:
            *dev_type = PCIXCC;
            break;
          default:
            *dev_type = NILDEV;
            break;
        }
	PDEBUG("query_on_line: Found available device %i\n",deviceNr);
	PDEBUG("query_on_line: Q depth: %i\n",*q_depth);
        PDEBUG("query_on_line: Dev type: %d\n",*dev_type);
	PDEBUG("query_on_line: Stat: %02x%02x%02x%02x\n",
	       stat_word.ap_q_stat_flags,
	       stat_word.ap_response_code,
	       stat_word.ap_reserved[0],
	       stat_word.ap_reserved[1]);
	break;
      case(3):
	switch (stat_word.ap_response_code){
	 case(AP_RESPONSE_NORMAL):            
	    stat = HD_ONLINE;
	    break_out = TRUE;
	    *q_depth = t_depth + 1;
            *dev_type = t_dev_type;
	    PDEBUG("query_on_line: Found available device %i\n",deviceNr);
	    PDEBUG("query_on_line: Q depth: %i\n",*q_depth);
            PDEBUG("query_on_line: Dev type: %d\n",*dev_type);
	    break;
	  case(AP_RESPONSE_Q_NOT_AVAIL):      
	    stat = HD_NOT_THERE;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_RESET_IN_PROGRESS):
	    PDEBUG("query_on_line: Found device being reset %i\n",deviceNr);
	    break;
	  case(AP_RESPONSE_DECONFIGURED):     
	    stat = HD_DECONFIGURED;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_CHECKSTOPPED):     
	    stat = HD_CHECKSTOPPED;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_BUSY):             
	    PDEBUG("query_on_line: Found device busy %i\n",deviceNr);
	    break;
	  default:
            break;
	} 
	break;
      default:
	stat = HD_NOT_THERE;
	break_out = TRUE;
    } 
    if (break_out == TRUE)
      break;
    udelay(5);
    ccode = testq(q_nr,&t_depth,&t_dev_type,&stat_word);
  } 
  return stat;
}; 
DEVSTAT reset_device (int deviceNr, int cdx, int resetNr)
{
  int q_nr;
  int ccode = 0;
  struct ap_status_word stat_word;
  DEVSTAT stat;
  BOOL break_out = FALSE;
  int dummy_qdepth;
  int dummy_devType;
  int i;
  q_nr =(deviceNr << (AP_MAX_CDX_BITL + AP_RQID_RESERVED_BITL)) + cdx;
  stat = DEV_GONE;
  ccode = resetq(q_nr,&stat_word);
  if (ccode > 3)
    return (DEV_RSQ_EXCEPTION);
  break_out = FALSE;
  for (i=0;i<resetNr;i++){
    switch(ccode) {
      case(0):
	stat = DEV_ONLINE;
	if (stat_word.ap_q_stat_flags & AP_Q_STATUS_EMPTY)
	  break_out = TRUE;
	break;
      case(3):
	switch (stat_word.ap_response_code){
	 case(AP_RESPONSE_NORMAL):             
	    stat = DEV_ONLINE;
	    if (stat_word.ap_q_stat_flags & AP_Q_STATUS_EMPTY)
	      break_out = TRUE;
	    break;
	  case(AP_RESPONSE_Q_NOT_AVAIL):       
	    stat = DEV_GONE;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_RESET_IN_PROGRESS): 
	    break;
	  case(AP_RESPONSE_DECONFIGURED):      
	    stat = DEV_GONE;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_CHECKSTOPPED):      
	    stat = DEV_GONE;
	    break_out = TRUE;
	    break;
	  case(AP_RESPONSE_BUSY):              
	    break;
	  default:
	    break;
	} 
	break;
      default:
	stat = DEV_GONE;
	break_out = TRUE;
    } 
    if (break_out == TRUE)
      break;
    udelay(5);
    ccode = testq(q_nr,&dummy_qdepth,&dummy_devType,&stat_word);
    if (ccode > 3) {
      stat = DEV_TSQ_EXCEPTION;
      break;
    }
  } 
  PDEBUG("Number of testq's needed for reset: %d\n",i);
  if (i >= resetNr) {
    stat = DEV_GONE;
  }
  return stat;
}; 
DEVSTAT send_to_AP(int dev_nr,
		   int cdx,
		   int msg_len,
		   UCHAR * msg_ext_p)
{
  int ccode = 0;
  struct ap_status_word stat_word;
  DEVSTAT stat;
  ((Z90C_REQUEST_MSG_EXT *)msg_ext_p)->q_nr =
		 (dev_nr << (AP_MAX_CDX_BITL + AP_RQID_RESERVED_BITL))
		   + cdx;
  PDEBUG("msg_len passed to sen: %d\n",msg_len);
  PDEBUG("q number passed to sen: %02x%02x%02x%02x\n",
	 msg_ext_p[0],msg_ext_p[1],msg_ext_p[2],msg_ext_p[3]);
  stat = DEV_GONE;
#ifdef SPECIAL_DEBUG  
  {
    int i;
    PRINTK(
        "Request header: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
        msg_ext_p[0], msg_ext_p[1], msg_ext_p[2], msg_ext_p[3],
        msg_ext_p[4], msg_ext_p[5], msg_ext_p[6], msg_ext_p[7],
        msg_ext_p[8], msg_ext_p[9], msg_ext_p[10], msg_ext_p[11]);
    for (i = 0; i < msg_len; i += 16)
    {
      PRINTK(
          "%04X: %02X%02X%02X%02X %02X%02X%02X%02X "
          "%02X%02X%02X%02X %02X%02X%02X%02X\n",
          i,
          msg_ext_p[i+12], msg_ext_p[i+13], msg_ext_p[i+14], msg_ext_p[i+15],
          msg_ext_p[i+16], msg_ext_p[i+17], msg_ext_p[i+18], msg_ext_p[i+19],
          msg_ext_p[i+20], msg_ext_p[i+21], msg_ext_p[i+22], msg_ext_p[i+23],
          msg_ext_p[i+24], msg_ext_p[i+25], msg_ext_p[i+26], msg_ext_p[i+27]);
    }
  }
#endif
  ccode = sen(msg_len,msg_ext_p,&stat_word);
  if (ccode > 3) {
    return (DEV_SEN_EXCEPTION);
  }
  PDEBUG("ccode returned by sen:  %u\n",ccode);
  PDEBUG("stat word returned by sen: %02x%02x%02x%02x\n",
	 stat_word.ap_q_stat_flags,
	 stat_word.ap_response_code,
	 stat_word.ap_reserved[0], stat_word.ap_reserved[1]);
  switch(ccode) {
    case(0):
      stat = DEV_ONLINE;
      break;
    case(1):
      stat = DEV_GONE;
      break;
    case(3):
      switch (stat_word.ap_response_code){
	case(AP_RESPONSE_NORMAL):             
	  stat = DEV_ONLINE;
	  break;
	case(AP_RESPONSE_Q_NOT_AVAIL):       
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_RESET_IN_PROGRESS): 
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_DECONFIGURED):      
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_CHECKSTOPPED):      
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_BUSY):              
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_Q_FULL):            
	  stat = DEV_QUEUE_FULL;
	  break;
	case(AP_RESPONSE_INDEX_TOO_BIG):     
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_NO_FIRST_PART):     
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_MESSAGE_TOO_BIG):   
	  stat = DEV_GONE;
	  break;
	default:                             
	  stat = DEV_GONE;
      } 
      break;
    default:
      stat = DEV_GONE;
  } 
  return stat;
}; 
DEVSTAT receive_from_AP(int dev_nr,
		      int cdx,
		      int resp_len,
		      unsigned char * response,
		      unsigned char * psmid)
{
  int ccode = 0;
  int q_nr = 0;
  struct ap_status_word stat_word;
  DEVSTAT stat;
  memset (response, 0, 8);        
				  
				  
  q_nr = (dev_nr << (AP_MAX_CDX_BITL + AP_RQID_RESERVED_BITL)) + cdx;
  stat = DEV_GONE;
  ccode = rec(q_nr, resp_len, response, psmid, &stat_word);
  PDEBUG("Response length after dq: %d\n",resp_len);
  if (ccode > 3)
    return (DEV_REC_EXCEPTION);
  PDEBUG("dq cc: %u,  st: %02x%02x%02x%02x\n",
	 ccode,
	 stat_word.ap_q_stat_flags,
	 stat_word.ap_response_code,
	 stat_word.ap_reserved[0],
	 stat_word.ap_reserved[1]);
  switch(ccode) {
    case(0):
      stat = DEV_ONLINE;
#ifdef SPECIAL_DEBUG  
      {
        int i;
        for (i = 0; i < resp_len; i += 16)
        {
          PRINTK(
              "%04X: %02X%02X%02X%02X %02X%02X%02X%02X "
              "%02X%02X%02X%02X %02X%02X%02X%02X\n",
              i,
              response[i+0], response[i+1], response[i+2], response[i+3],
              response[i+4], response[i+5], response[i+6], response[i+7],
              response[i+8], response[i+9], response[i+10], response[i+11],
              response[i+12], response[i+13], response[i+14], response[i+15]);
        }
      }
#endif
      break;
    case(3):
      switch (stat_word.ap_response_code){
       case(AP_RESPONSE_NORMAL):             
	  stat = DEV_ONLINE;
	  break;
	case(AP_RESPONSE_Q_NOT_AVAIL):       
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_RESET_IN_PROGRESS): 
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_DECONFIGURED):      
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_CHECKSTOPPED):      
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_BUSY):              
	  stat = DEV_GONE;
	  break;
	case(AP_RESPONSE_NO_PENDING_REPLY):  
	  if ((stat_word.ap_q_stat_flags && AP_Q_STATUS_EMPTY) ==
						    AP_Q_STATUS_EMPTY)
	    stat = DEV_EMPTY;
	  else
	    stat = DEV_NO_WORK;
	  break;
	case(AP_RESPONSE_INDEX_TOO_BIG):     
	  stat = DEV_BAD_MESSAGE;
	  break;
	case(AP_RESPONSE_NO_FIRST_PART):     
	  stat = DEV_BAD_MESSAGE;
	  break;
	case(AP_RESPONSE_MESSAGE_TOO_BIG):   
	  stat = DEV_BAD_MESSAGE;
	  break;
	default:                             
	  stat = DEV_GONE;
      } 
      break;
    default:
      stat = DEV_GONE;
  } 
  return stat;
}; 
int pad_msg(UCHAR * buffer, int  totalLength, int msgLength)
{
  int padLen;
  UCHAR * ptr;
  int j;
  ptr = buffer; 
  for (j=0;j<totalLength - msgLength;j++){
    if (ptr[j] != 0)
      break;
  }
  padLen = j -3; 
  if (padLen < 8)
    return SEN_PADDING_ERROR;
  ptr[0] = 0x00;
  ptr[1] = 0x02;
  memcpy(ptr+2,stat_pad,padLen);
  ptr[padLen + 2] = 0x00;
  return OK;
} 
int is_common_public_key (unsigned char * keyP, int keyL)
{
  int rv = 0;
  int i;
  unsigned char * p;
  int l;
  for (i=0;i<keyL;i++) {
    if (keyP[i])
      break;
  }
  p = keyP + i;
  l = keyL - i;
  if (((l == 1) && (p[0] == 3)) ||
      ((l == 3) && (p[0] == 1) && (p[1] == 0) && (p[2] == 1)))
    rv = 1;
  return (rv);
}
int convert_ICAMEX_msg_to_type4MEX_msg (int icaMsg_l,
                                   ica_rsa_modexpo_t * icaMex_p,
                                   int caller_hdr,
                                   HWRD  function,
                                   UCHAR * pad_rule,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE4_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int allocSize = 0;
  UCHAR * outMsg_p = NULL;
  Z90C_TYPE4_MSG * tmpTgt_p;
  UCHAR * modTgt;
  UCHAR * expTgt;
  UCHAR * inpTgt;
  UCHAR * origInpTgt;
  int modTgt_l;
  int expTgt_l;
  int inpTgt_l;
  modLen = icaMex_p->inputdatalength;
  allocSize = ((modLen <= 128) ? Z90C_TYPE4_SME_LEN : Z90C_TYPE4_LME_LEN) +
              caller_hdr;
  do {
    outMsg_p = (UCHAR *)z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tmpTgt_p = (Z90C_TYPE4_MSG *)(outMsg_p + caller_hdr);
    tmpTgt_p->sme.z90c_type4_sme_hdr.z90c_type4_code =
                                                    Z90C_TYPE4_TYPE_CODE;
    tmpTgt_p->sme.z90c_type4_sme_hdr.z90c_type4_req_code =
                                                    Z90C_TYPE4_REQU_CODE;
    if (modLen <= 128) {
      tmpTgt_p->sme.z90c_type4_sme_hdr.z90c_type4_m_fmt =
                                                      Z90C_TYPE4_SME_FMT;
      tmpTgt_p->sme.z90c_type4_sme_hdr.z90c_type4_m_len =
                                                      Z90C_TYPE4_SME_LEN;
      modTgt = tmpTgt_p->sme.z90c_type4_sme_mod;
      modTgt_l = sizeof(tmpTgt_p->sme.z90c_type4_sme_mod);
      expTgt = tmpTgt_p->sme.z90c_type4_sme_exp;
      expTgt_l = sizeof(tmpTgt_p->sme.z90c_type4_sme_exp);
      inpTgt = tmpTgt_p->sme.z90c_type4_sme_msg;
      inpTgt_l = sizeof(tmpTgt_p->sme.z90c_type4_sme_msg);
    }
    else {
      tmpTgt_p->lme.z90c_type4_lme_hdr.z90c_type4_m_fmt =
                                                      Z90C_TYPE4_LME_FMT;
      tmpTgt_p->lme.z90c_type4_lme_hdr.z90c_type4_m_len =
                                                      Z90C_TYPE4_LME_LEN;
      modTgt = tmpTgt_p->lme.z90c_type4_lme_mod;
      modTgt_l = sizeof(tmpTgt_p->lme.z90c_type4_lme_mod);
      expTgt = tmpTgt_p->lme.z90c_type4_lme_exp;
      expTgt_l = sizeof(tmpTgt_p->lme.z90c_type4_lme_exp);
      inpTgt = tmpTgt_p->lme.z90c_type4_lme_msg;
      inpTgt_l = sizeof(tmpTgt_p->lme.z90c_type4_lme_msg);
    }
    modTgt += (modTgt_l - modLen);
    if ((rv=copy_from_user(modTgt,icaMex_p->n_modulus,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(modTgt,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    expTgt += (expTgt_l - modLen);          
    if ((rv=copy_from_user(expTgt,icaMex_p->b_key,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(expTgt,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    origInpTgt = inpTgt;
    inpTgt += (inpTgt_l - modLen);
    if ((rv=copy_from_user(inpTgt,icaMex_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(inpTgt,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
}; 
int convert_ICACRT_msg_to_type4CRT_msg (int icaMsg_l,
                                   ica_rsa_modexpo_crt_t * icaMsg_p,
                                   int caller_hdr,
                                   HWRD  function,
                                   UCHAR * pad_rule,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE4_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int shortLen = 0;
  int longLen = 0;
  int allocSize = 0;
  UCHAR * outMsg_p = NULL;
  Z90C_TYPE4_MSG * tmpTgt_p;
  UCHAR * pTgt;
  UCHAR * qTgt;
  UCHAR * dpTgt;
  UCHAR * dqTgt;
  UCHAR * uTgt;
  UCHAR * inpTgt;
  UCHAR * origInpTgt;
  int pTgt_l;
  int qTgt_l;
  int dpTgt_l;
  int dqTgt_l;
  int uTgt_l;
  int inpTgt_l;
  modLen = icaMsg_p->inputdatalength;
  shortLen = modLen / 2;
  longLen = shortLen + 8;
  allocSize = ((modLen <= 128) ? Z90C_TYPE4_SCR_LEN : Z90C_TYPE4_LCR_LEN) +
              caller_hdr;
  do {
    outMsg_p = (UCHAR *)z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tmpTgt_p = (Z90C_TYPE4_MSG *) (outMsg_p + caller_hdr); 
    tmpTgt_p->scr.z90c_type4_scr_hdr.z90c_type4_code =
                                                   Z90C_TYPE4_TYPE_CODE;
    tmpTgt_p->scr.z90c_type4_scr_hdr.z90c_type4_req_code =
                                                   Z90C_TYPE4_REQU_CODE;
    if (modLen <= 128) {
      tmpTgt_p->scr.z90c_type4_scr_hdr.z90c_type4_m_fmt =
                                                     Z90C_TYPE4_SCR_FMT;
      tmpTgt_p->scr.z90c_type4_scr_hdr.z90c_type4_m_len =
                                                     Z90C_TYPE4_SCR_LEN;
      pTgt = tmpTgt_p->scr.z90c_type4_scr_p;
      pTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_p);
      qTgt = tmpTgt_p->scr.z90c_type4_scr_q;
      qTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_q);
      dpTgt = tmpTgt_p->scr.z90c_type4_scr_dp;
      dpTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_dp);
      dqTgt = tmpTgt_p->scr.z90c_type4_scr_dq;
      dqTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_dq);
      uTgt = tmpTgt_p->scr.z90c_type4_scr_u;
      uTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_u);
      inpTgt = tmpTgt_p->scr.z90c_type4_scr_msg;
      inpTgt_l = sizeof(tmpTgt_p->scr.z90c_type4_scr_msg);
    }
    else {
      tmpTgt_p->lcr.z90c_type4_lcr_hdr.z90c_type4_m_fmt =
                                                     Z90C_TYPE4_LCR_FMT;
      tmpTgt_p->lcr.z90c_type4_lcr_hdr.z90c_type4_m_len =
                                                     Z90C_TYPE4_LCR_LEN;
      pTgt = tmpTgt_p->lcr.z90c_type4_lcr_p;
      pTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_p);
      qTgt = tmpTgt_p->lcr.z90c_type4_lcr_q;
      qTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_q);
      dpTgt = tmpTgt_p->lcr.z90c_type4_lcr_dp;
      dpTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_dp);
      dqTgt = tmpTgt_p->lcr.z90c_type4_lcr_dq;
      dqTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_dq);
      uTgt = tmpTgt_p->lcr.z90c_type4_lcr_u;
      uTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_u);
      inpTgt = tmpTgt_p->lcr.z90c_type4_lcr_msg;
      inpTgt_l = sizeof(tmpTgt_p->lcr.z90c_type4_lcr_msg);
    }
    pTgt += (pTgt_l - longLen);
    if ((rv=copy_from_user(pTgt,icaMsg_p->np_prime,longLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(pTgt,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    qTgt += (qTgt_l - shortLen);
    if ((rv=copy_from_user(qTgt,icaMsg_p->nq_prime,shortLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(qTgt,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    dpTgt += (dpTgt_l - longLen);
    if ((rv=copy_from_user(dpTgt,icaMsg_p->bp_key,longLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(dpTgt,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    dqTgt += (dqTgt_l - shortLen);
    if ((rv=copy_from_user(dqTgt,icaMsg_p->bq_key,shortLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(dqTgt,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    uTgt += (uTgt_l - longLen);
    if ((rv=copy_from_user(uTgt,icaMsg_p->u_mult_inv,longLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(uTgt,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    origInpTgt = inpTgt;
    inpTgt += (inpTgt_l - modLen);
    if ((rv=copy_from_user(inpTgt,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(inpTgt,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICAMEX_msg_to_type6MEX_de_msg (int icaMsg_l,
                                   ica_rsa_modexpo_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRB * cprb_p;
  Z90C_CCA_PRIVATE_EXT_ME * key_p;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  allocSize = FIXED_TYPE6_ME_LEN + modLen;
  totCPRBLen = allocSize - sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprb);
  allocSize = 4*((allocSize + 3)/4) + caller_hdr;
  vudLen = 2 + modLen;        
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdr,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = 4*((totCPRBLen+3)/4);
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRB_SIZE;
    tgt_p += sizeof(struct z90c_type6_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_cprb,sizeof(struct z90c_cprb));
    cprb_p = (Z90C_CPRB *) tgt_p;
    cprb_p->usage_domain[0]= (UCHAR)cdx;
    itoLe2(&parmBlock_l,cprb_p->req_parml);
    itoLe2((int *)&(tp6Hdr_p->z90c_type6_hdr_FrCardLen1),cprb_p->rpl_parml);
    tgt_p += sizeof(Z90C_CPRB);
    memcpy(tgt_p,(UCHAR *)&stat_pkd_function_and_rules,
           sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    itoLe2(&vudLen,tgt_p); 
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += modLen;
    memcpy(tgt_p,(UCHAR *)&stat_pkd_T6_keyBlock_hdr,
           sizeof(struct pkd_T6_keyBlock_hdr));
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_pvt_me_key,
           sizeof(struct z90c_cca_private_ext_ME));
    key_p = (Z90C_CCA_PRIVATE_EXT_ME *)tgt_p;
    tmpTgt_p = key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_pvt_exp +
               sizeof(key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_pvt_exp) -
               modLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->b_key, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    if (is_common_public_key(tmpTgt_p, modLen)) {
      rv = SEN_NOT_AVAIL;
      break;
    }
    tmpTgt_p = key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_mod +
               sizeof(key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_mod) -
               modLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->n_modulus, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    key_p->pubMESec.z90c_cca_pub_sec_mod_bit_len = modBitLen;
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICAMEX_msg_to_type6MEX_en_msg (int icaMsg_l,
                                   ica_rsa_modexpo_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int expLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRB * cprb_p;
  Z90C_CCA_PUBLIC_KEY * key_p;
  int keyLen = 0;
  UCHAR temp_exp[256];
  UCHAR * exp_p;
  struct pkd_T6_keyBlock_hdr * keyb_p;
  int i;
  int padLen;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  if ((rv=copy_from_user(temp_exp, icaMsg_p->b_key, modLen))!=0)
    return SEN_RELEASED;
  if (!(isNotEmpty(temp_exp,modLen)))
    return SEN_USER_ERROR;
  exp_p = temp_exp;
  for (i=0;i<modLen;i++){
    if (exp_p[i])
      break;
  }
  if (i < modLen) {
    expLen = modLen - i;
    exp_p += i;
  }
  else {
    return SEN_OPERAND_INVALID;
  }
  PDEBUG("expLen after computation: %08x\n",expLen);
  allocSize = FIXED_TYPE6_ME_EN_LEN + 2*modLen + expLen;
  totCPRBLen = allocSize - sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprb);
  allocSize = 4*((allocSize + 3)/4) + caller_hdr;
  vudLen = 2 + modLen;        
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdr,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = 4*((totCPRBLen+3)/4);
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRB_SIZE;
    memcpy(tp6Hdr_p->z90c_type6_scc_function,
           stat_PE_function_code,
           sizeof(stat_PE_function_code));
    tgt_p += sizeof(struct z90c_type6_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_cprb,sizeof(struct z90c_cprb));
    cprb_p = (Z90C_CPRB *) tgt_p;
    cprb_p->usage_domain[0]= (UCHAR)cdx;
    itoLe2((int *)&(tp6Hdr_p->z90c_type6_hdr_FrCardLen1),cprb_p->rpl_parml);
    tgt_p += sizeof(Z90C_CPRB);
    memcpy(tgt_p,(UCHAR *)&stat_pke_function_and_rules,
             sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    if (tgt_p[0] != 0 || tgt_p[1] != 0x02) {
      rv = SEN_NOT_AVAIL;
      break;
    }
    for (i=2;i<modLen;i++) {
      if (tgt_p[i] == 0)
        break;
    }
    if (i < 9 || i > modLen - 2) {
      rv = SEN_NOT_AVAIL;
      break;
    }
    padLen = i + 1;
    vudLen = modLen - padLen;
    memmove(tgt_p, tgt_p+padLen, vudLen);
    tgt_p -= 2;
    vudLen += 2;
    itoLe2(&vudLen,tgt_p); 
    tgt_p += (vudLen);
    keyb_p = (struct pkd_T6_keyBlock_hdr *)tgt_p;
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_pub_key,
           sizeof(stat_pub_key));
    key_p = (Z90C_CCA_PUBLIC_KEY *)tgt_p;
    tmpTgt_p = key_p->pubSec.z90c_cca_pub_sec_expmod;
    memcpy(tmpTgt_p, exp_p, expLen);
    tmpTgt_p += expLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->n_modulus, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    key_p->pubSec.z90c_cca_pub_sec_mod_bit_len = modBitLen;
    key_p->pubSec.z90c_cca_pub_sec_mod_byte_len = modLen;
    key_p->pubSec.z90c_cca_pub_sec_exp_len = expLen;
    key_p->pubSec.z90c_cca_pub_sec_length =
                             12 +
                             modLen + expLen;
    keyLen = key_p->pubSec.z90c_cca_pub_sec_length +
             sizeof(Z90C_CCA_TOKEN_HDR);
    key_p->pubHdr.z90c_cca_tkn_length = keyLen;
    keyLen += 4;
    itoLe2(&keyLen,keyb_p->ulen);
    keyLen += 2;
    itoLe2(&keyLen,keyb_p->blen);
    parmBlock_l -= padLen;
    itoLe2(&parmBlock_l,cprb_p->req_parml);
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICACRT_msg_to_type6CRT_msg (int icaMsg_l,
                                   ica_rsa_modexpo_crt_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  int shortLen = 0;
  int longLen = 0;
  int padLen = 0;
  int keyPartsLen = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int tmp_l;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRB * cprb_p;
  Z90C_CCA_TOKEN_HDR * keyHdr_p;
  Z90C_CCA_PRIVATE_EXT_CRT_SEC * pvtSec_p;
  Z90C_CCA_PUBLIC_SEC * pubSec_p;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  shortLen = modLen / 2;
  longLen = 8 + shortLen;
  keyPartsLen = 3*longLen + 2*shortLen;  
  padLen = (8 - (keyPartsLen % 8)) % 8;
  keyPartsLen += padLen + modLen;
  allocSize = FIXED_TYPE6_CR_LEN + keyPartsLen + modLen;
  totCPRBLen = allocSize -  sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprb);
  vudLen = 2 + modLen;     
  allocSize = 4*((allocSize + 3)/4) + caller_hdr;
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdr,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = 4*((totCPRBLen+3)/4);
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRB_SIZE;
    tgt_p += sizeof(struct z90c_type6_hdr);
    cprb_p = (Z90C_CPRB *) tgt_p;
    memcpy(tgt_p,(UCHAR *)&stat_cprb,sizeof(struct z90c_cprb));
    cprb_p->usage_domain[0]= *((UCHAR *)(&(cdx))+3);
    itoLe2(&parmBlock_l,cprb_p->req_parml);
    memcpy(cprb_p->rpl_parml,
           cprb_p->req_parml,
           sizeof(cprb_p->req_parml));
    tgt_p += sizeof(Z90C_CPRB);
    memcpy(tgt_p,(UCHAR *)&stat_pkd_function_and_rules,
           sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    itoLe2(&vudLen,tgt_p); 
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += modLen;
    tmp_l = sizeof(struct pkd_T6_keyBlock_hdr) +
            sizeof(struct z90c_cca_token_hdr) +
            sizeof(struct z90c_cca_private_ext_CRT_sec) +
            0x0f +          
            keyPartsLen;
    itoLe2(&tmp_l,tgt_p);
    tmpTgt_p = tgt_p + 2;
    tmp_l -= 2;
    itoLe2(&tmp_l,tmpTgt_p);
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdr);
    keyHdr_p = (Z90C_CCA_TOKEN_HDR *)tgt_p;
    keyHdr_p->z90c_cca_tkn_hdr_id = Z90C_CCA_TKN_HDR_ID_EXT;
    tmp_l -= 4;
    keyHdr_p->z90c_cca_tkn_length = tmp_l;
    tgt_p += sizeof(struct z90c_cca_token_hdr);
    pvtSec_p = (Z90C_CCA_PRIVATE_EXT_CRT_SEC *)tgt_p;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_id = Z90C_CCA_PVT_EXT_CRT_SEC_ID_PVT;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_length =
                    sizeof(struct z90c_cca_private_ext_CRT_sec)+keyPartsLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_fmt = Z90C_CCA_PVT_EXT_CRT_SEC_FMT_CL;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_usage = Z90C_CCA_PVT_USAGE_ALL;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_p_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_q_len = shortLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_dp_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_dq_len = shortLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_u_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_mod_len = modLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_pad_len = padLen;
    tgt_p += sizeof(struct z90c_cca_private_ext_CRT_sec);
    if ((copy_from_user(tgt_p,icaMsg_p->np_prime,longLen))!=OK){  
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->nq_prime,shortLen))!=OK){ 
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += shortLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->bp_key,longLen))!=OK){    
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->bq_key,shortLen))!=OK){   
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += shortLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->u_mult_inv,longLen))!=OK){  
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if (padLen != 0)
      tgt_p += padLen;
    memset(tgt_p,0xFF,modLen);
    tgt_p += modLen;
    memcpy(tgt_p,(UCHAR *)&stat_cca_public_sec,
                 sizeof(struct z90c_cca_public_sec));
    pubSec_p = (Z90C_CCA_PUBLIC_SEC *) tgt_p;
    pubSec_p->z90c_cca_pub_sec_mod_bit_len = modBitLen;
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICAMEX_msg_to_type6MEX_de_msgX (int icaMsg_l,
                                   ica_rsa_modexpo_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRBX * cprbx_p;
  Z90C_CCA_PRIVATE_EXT_ME * key_p;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  allocSize = FIXED_TYPE6_ME_LENX + modLen;
  totCPRBLen = allocSize - sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprbx);
  allocSize += caller_hdr;
  vudLen = 2 + modLen;        
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdrX,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = totCPRBLen;
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRBX_SIZE;
    tgt_p += sizeof(struct z90c_type6_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_cprbx,sizeof(struct z90c_cprbx));
    cprbx_p = (Z90C_CPRBX *) tgt_p;
    cprbx_p->domain = (USHORT)cdx;
    cprbx_p->req_parml = parmBlock_l;
    cprbx_p->rpl_msgbl = RESPONSE_CPRBX_SIZE;
    tgt_p += sizeof(Z90C_CPRBX);
    memcpy(tgt_p,(UCHAR *)&stat_pkd_function_and_rulesX,
           sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    *((short *)tgt_p) = (short) vudLen;
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += modLen;
    memcpy(tgt_p,(UCHAR *)&stat_pkd_T6_keyBlock_hdrX,
           sizeof(struct pkd_T6_keyBlock_hdrX));
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdrX);
    memcpy(tgt_p,(UCHAR *)&stat_pvt_me_key,
           sizeof(struct z90c_cca_private_ext_ME));
    key_p = (Z90C_CCA_PRIVATE_EXT_ME *)tgt_p;
    tmpTgt_p = key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_pvt_exp +
               sizeof(key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_pvt_exp) -
               modLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->b_key, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    if (is_common_public_key(tmpTgt_p, modLen)) {
      PRINTK("Common public key used for modex decrypt\n");
      rv = SEN_NOT_AVAIL;
      break;
    }
    tmpTgt_p = key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_mod +
               sizeof(key_p->pvtMESec.z90c_cca_pvt_ext_ME_sec_mod) -
               modLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->n_modulus, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    key_p->pubMESec.z90c_cca_pub_sec_mod_bit_len = modBitLen;
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICAMEX_msg_to_type6MEX_en_msgX (int icaMsg_l,
                                   ica_rsa_modexpo_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int expLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRBX * cprbx_p;
  Z90C_CCA_PUBLIC_KEY * key_p;
  int keyLen = 0;
  UCHAR temp_exp[256];
  UCHAR * exp_p;
  struct pkd_T6_keyBlock_hdrX * keyb_p;
  int i;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  if ((rv=copy_from_user(temp_exp, icaMsg_p->b_key, modLen))!=0)
    return SEN_RELEASED;
  if (!(isNotEmpty(temp_exp,modLen)))
    return SEN_USER_ERROR;
  exp_p = temp_exp;
  for (i=0;i<modLen;i++){
    if (exp_p[i])
      break;
  }
  if (i < modLen) {
    expLen = modLen - i;
    exp_p += i;
  }
  else {
    return SEN_OPERAND_INVALID;
  }
  PDEBUG("expLen after computation: %08x\n",expLen);
  allocSize = FIXED_TYPE6_ME_EN_LENX + 2*modLen + expLen;
  totCPRBLen = allocSize - sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprbx);
  allocSize = allocSize + caller_hdr;
  vudLen = 2 + modLen;        
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdrX,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = totCPRBLen;
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRBX_SIZE;
    memcpy(tp6Hdr_p->z90c_type6_scc_function,
           stat_PE_function_code,
           sizeof(stat_PE_function_code));
    tgt_p += sizeof(struct z90c_type6_hdr);
    memcpy(tgt_p,(UCHAR *)&stat_cprbx,sizeof(struct z90c_cprbx));
    cprbx_p = (Z90C_CPRBX *) tgt_p;
    cprbx_p->domain = (USHORT)cdx;
    cprbx_p->rpl_msgbl = RESPONSE_CPRBX_SIZE;
    tgt_p += sizeof(Z90C_CPRBX);
    memcpy(tgt_p,(UCHAR *)&stat_pke_function_and_rulesX,
             sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p -= 2;
    *((short *)tgt_p) = (short) vudLen;
    tgt_p += (vudLen);
    keyb_p = (struct pkd_T6_keyBlock_hdrX *)tgt_p;
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdrX);
    memcpy(tgt_p,(UCHAR *)&stat_pub_key,
           sizeof(stat_pub_key));
    key_p = (Z90C_CCA_PUBLIC_KEY *)tgt_p;
    tmpTgt_p = key_p->pubSec.z90c_cca_pub_sec_expmod;
    memcpy(tmpTgt_p, exp_p, expLen);
    tmpTgt_p += expLen;
    if ((rv=copy_from_user(tmpTgt_p, icaMsg_p->n_modulus, modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tmpTgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    key_p->pubSec.z90c_cca_pub_sec_mod_bit_len = modBitLen;
    key_p->pubSec.z90c_cca_pub_sec_mod_byte_len = modLen;
    key_p->pubSec.z90c_cca_pub_sec_exp_len = expLen;
    key_p->pubSec.z90c_cca_pub_sec_length =
                             12 +
                             modLen + expLen;
    keyLen = key_p->pubSec.z90c_cca_pub_sec_length +
             sizeof(Z90C_CCA_TOKEN_HDR);
    key_p->pubHdr.z90c_cca_tkn_length = keyLen;
    keyLen += 4;
    keyb_p->ulen = (USHORT)keyLen;
    keyLen += 2;
    keyb_p->blen = (USHORT)keyLen;
    cprbx_p->req_parml = parmBlock_l;
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_ICACRT_msg_to_type6CRT_msgX (int icaMsg_l,
                                   ica_rsa_modexpo_crt_t * icaMsg_p,
                                   int caller_hdr,
                                   int cdx,
                                   int * z90cMsg_l_p,
                                   Z90C_TYPE6_MSG * z90cMsg_p)
{
  int rv = OK;
  int modLen = 0;
  int vudLen = 0;
  int allocSize = 0;
  int totCPRBLen = 0;
  int parmBlock_l = 0;
  int shortLen = 0;
  int longLen = 0;
  int padLen = 0;
  int keyPartsLen = 0;
  Z90C_TYPE6_MSG * outMsg_p = NULL;
  UCHAR * tgt_p;
  UCHAR * tmpTgt_p;
  int tmp_l;
  int modBitLen = 0;
  Z90C_TYPE6_HDR * tp6Hdr_p;
  Z90C_CPRBX * cprbx_p;
  Z90C_CCA_TOKEN_HDR * keyHdr_p;
  Z90C_CCA_PRIVATE_EXT_CRT_SEC * pvtSec_p;
  Z90C_CCA_PUBLIC_SEC * pubSec_p;
  modLen = icaMsg_p->inputdatalength;
  modBitLen = 8*modLen;
  shortLen = modLen / 2;
  longLen = 8 + shortLen;
  keyPartsLen = 3*longLen + 2*shortLen;  
  padLen = (8 - (keyPartsLen % 8)) % 8;
  keyPartsLen += padLen + modLen;
  allocSize = FIXED_TYPE6_CR_LENX + keyPartsLen + modLen;
  totCPRBLen = allocSize -  sizeof(struct z90c_type6_hdr);
  parmBlock_l = totCPRBLen - sizeof(struct z90c_cprbx);
  vudLen = 2 + modLen;     
  allocSize = allocSize + caller_hdr;
  do {
    outMsg_p = z90cMsg_p;
    memset(outMsg_p,0,allocSize);
    tgt_p = (UCHAR *)outMsg_p + caller_hdr; 
    memcpy(tgt_p,(UCHAR *)&stat_type6_hdrX,sizeof(struct z90c_type6_hdr));
    tp6Hdr_p = (Z90C_TYPE6_HDR *)tgt_p;
    tp6Hdr_p->z90c_type6_hdr_ToCardLen1 = totCPRBLen;
    tp6Hdr_p->z90c_type6_hdr_FrCardLen1 = RESPONSE_CPRBX_SIZE;
    tgt_p += sizeof(struct z90c_type6_hdr);
    cprbx_p = (Z90C_CPRBX *) tgt_p;
    memcpy(tgt_p,(UCHAR *)&stat_cprbx,sizeof(struct z90c_cprbx));
    cprbx_p->domain = (USHORT)cdx;
    cprbx_p->req_parml = parmBlock_l;
    cprbx_p->rpl_msgbl = parmBlock_l;
    tgt_p += sizeof(Z90C_CPRBX);
    memcpy(tgt_p,(UCHAR *)&stat_pkd_function_and_rulesX,
           sizeof(struct function_and_rules_block));
    tgt_p += sizeof(struct function_and_rules_block);
    *((short *)tgt_p) = (short) vudLen;
    tgt_p += 2;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->inputdata,modLen))!=OK){
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,modLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += modLen;
    tmp_l = sizeof(struct pkd_T6_keyBlock_hdr) +
            sizeof(struct z90c_cca_token_hdr) +
            sizeof(struct z90c_cca_private_ext_CRT_sec) +
            0x0f +          
            keyPartsLen;
    *((short *)tgt_p) = (short) tmp_l;
    tmpTgt_p = tgt_p + 2;
    tmp_l -= 2;
    *((short *)tmpTgt_p) = (short) tmp_l;
    tgt_p += sizeof(struct pkd_T6_keyBlock_hdr);
    keyHdr_p = (Z90C_CCA_TOKEN_HDR *)tgt_p;
    keyHdr_p->z90c_cca_tkn_hdr_id = Z90C_CCA_TKN_HDR_ID_EXT;
    tmp_l -= 4;
    keyHdr_p->z90c_cca_tkn_length = tmp_l;
    tgt_p += sizeof(struct z90c_cca_token_hdr);
    pvtSec_p = (Z90C_CCA_PRIVATE_EXT_CRT_SEC *)tgt_p;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_id = Z90C_CCA_PVT_EXT_CRT_SEC_ID_PVT;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_length =
                    sizeof(struct z90c_cca_private_ext_CRT_sec)+keyPartsLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_fmt = Z90C_CCA_PVT_EXT_CRT_SEC_FMT_CL;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_usage = Z90C_CCA_PVT_USAGE_ALL;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_p_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_q_len = shortLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_dp_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_dq_len = shortLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_u_len = longLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_mod_len = modLen;
    pvtSec_p->z90c_cca_pvt_ext_CRT_sec_pad_len = padLen;
    tgt_p += sizeof(struct z90c_cca_private_ext_CRT_sec);
    if ((copy_from_user(tgt_p,icaMsg_p->np_prime,longLen))!=OK){  
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->nq_prime,shortLen))!=OK){ 
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += shortLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->bp_key,longLen))!=OK){    
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->bq_key,shortLen))!=OK){   
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,shortLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += shortLen;
    if ((rv=copy_from_user(tgt_p,icaMsg_p->u_mult_inv,longLen))!=OK){  
      rv = SEN_RELEASED;
      break;
    }
    if (!(isNotEmpty(tgt_p,longLen))){
      rv = SEN_USER_ERROR;
      break;
    }
    tgt_p += longLen;
    if (padLen != 0)
      tgt_p += padLen;
    memset(tgt_p,0xFF,modLen);
    tgt_p += modLen;
    memcpy(tgt_p,(UCHAR *)&stat_cca_public_sec,
                 sizeof(struct z90c_cca_public_sec));
    pubSec_p = (Z90C_CCA_PUBLIC_SEC *) tgt_p;
    pubSec_p->z90c_cca_pub_sec_mod_bit_len = modBitLen;
  } while(0);
  if (rv == OK) {
    *z90cMsg_l_p = allocSize - caller_hdr;
  }
  return (rv);
} 
int convert_request (UCHAR * buffer,
                    int func,
                    HWRD  function,
                    UCHAR * pad_rule,
                    int cdx,
                    CDEVICE_TYPE dev_type,
                    int * msg_l_p,
                    UCHAR * msg_p)
{
  int rv = OK;
  int caller_hdr = 12;    
  switch(dev_type) {
    case PCICA:
      if (func==ICARSAMODEXPO)
        rv = convert_ICAMEX_msg_to_type4MEX_msg(sizeof(ica_rsa_modexpo_t),
                                           (ica_rsa_modexpo_t *)buffer,
                                           caller_hdr,
                                           function,
                                           pad_rule,
                                           msg_l_p,
                                           (Z90C_TYPE4_MSG *)msg_p);
      else
        rv = convert_ICACRT_msg_to_type4CRT_msg(sizeof(ica_rsa_modexpo_crt_t),
                                           (ica_rsa_modexpo_crt_t *)buffer,
                                           caller_hdr,
                                           function,
                                           pad_rule,
                                           msg_l_p,
                                           (Z90C_TYPE4_MSG *)msg_p);
      break;
    case PCICC:
      if (func==ICARSAMODEXPO)
        if (function == PCI_FUNC_KEY_ENCRYPT)
          rv = convert_ICAMEX_msg_to_type6MEX_en_msg(sizeof(ica_rsa_modexpo_t),
                                               (ica_rsa_modexpo_t *)buffer,
                                               caller_hdr,
                                               cdx,
                                               msg_l_p,
                                               (Z90C_TYPE6_MSG *)msg_p);
        else
          rv = convert_ICAMEX_msg_to_type6MEX_de_msg(sizeof(ica_rsa_modexpo_t),
                                               (ica_rsa_modexpo_t *)buffer,
                                               caller_hdr,
                                               cdx,
                                               msg_l_p,
                                               (Z90C_TYPE6_MSG *)msg_p);
      else
        rv = convert_ICACRT_msg_to_type6CRT_msg(sizeof(ica_rsa_modexpo_crt_t),
                                           (ica_rsa_modexpo_crt_t *)buffer,
                                           caller_hdr,
                                           cdx,
                                           msg_l_p,
                                           (Z90C_TYPE6_MSG *)msg_p);
      break;
    case PCIXCC:
      if (func==ICARSAMODEXPO) {
        if (function == PCI_FUNC_KEY_ENCRYPT) {
          rv = convert_ICAMEX_msg_to_type6MEX_en_msgX(sizeof(ica_rsa_modexpo_t),
                                               (ica_rsa_modexpo_t *)buffer,
                                               caller_hdr,
                                               cdx,
                                               msg_l_p,
                                               (Z90C_TYPE6_MSG *)msg_p);
        }
        else {
          rv = convert_ICAMEX_msg_to_type6MEX_de_msgX(sizeof(ica_rsa_modexpo_t),
                                               (ica_rsa_modexpo_t *)buffer,
                                               caller_hdr,
                                               cdx,
                                               msg_l_p,
                                               (Z90C_TYPE6_MSG *)msg_p);
        }
      }
      else {
        rv = convert_ICACRT_msg_to_type6CRT_msgX(sizeof(ica_rsa_modexpo_crt_t),
                                           (ica_rsa_modexpo_crt_t *)buffer,
                                           caller_hdr,
                                           cdx,
                                           msg_l_p,
                                           (Z90C_TYPE6_MSG *)msg_p);
      }
      break;
    default:
      break;
  }; 
  return (rv);
} 
int convert_response (UCHAR * response,
                     CDEVICE_TYPE dev_type,
                     int icaMsg_l,
                     UCHAR * buffer,
                     HWRD  function,
                     UCHAR *pad_rule,
                     int *respbufflen_p,
                     UCHAR *respbuff)
{
  int rv = OK;
  UCHAR * src_p = NULL;
  int     src_l = 0;
  struct z90c_cprb * cprb_p;
  struct z90c_cprbx * cprbx_p;
  Z90C_TYPE86_HDR * t86h_p;
  Z90C_TYPE84_HDR * t84h_p;
  Z90C_TYPE82_HDR * t82h_p;
  UCHAR * tgt_p;
  ica_rsa_modexpo_t * icaMsg_p;
  int reply_code = OK;
  int service_rc = OK;
  int service_rs = OK;
  t86h_p = (Z90C_TYPE86_HDR *)response;
  t84h_p = (Z90C_TYPE84_HDR *)response;
  t82h_p = (Z90C_TYPE82_HDR *)response;
  icaMsg_p = (ica_rsa_modexpo_t *)buffer;
  switch(t82h_p->z90c_type82_hdr_type) {
    case Z90C_TYPE82_RSP_CODE:
      reply_code = t82h_p->z90c_type82_hdr_reply;
      rv = 4;                         
      PRINTK("Hardware error\n");
      src_p = (unsigned char *)t82h_p;
      PRINTK("Type 82 Message Header: %02x%02x%02x%02x%02x%02x%02x%02x\n",
             src_p[0],
             src_p[1],
             src_p[2],
             src_p[3],
             src_p[4],
             src_p[5],
             src_p[6],
             src_p[7]);
      break;
    case Z90C_TYPE84_RSP_CODE:
      src_l = icaMsg_p->outputdatalength;
      src_p = response + (int)t84h_p->z90c_type84_len - src_l;
      break;
    case Z90C_TYPE86_RSP_CODE:
      reply_code = t86h_p->z90c_type86_hdr_reply;
      if (t86h_p->z90c_type86_hdr_fmt == Z90C_TYPE86_FMT2) {
        if (reply_code == OK) {
          cprb_p = (Z90C_CPRB *)(response +
                                 sizeof(struct z90c_type86_fmt2_msg));
	  if (cprb_p->cprb_ver_id != 0x02) {  
            le2toI(cprb_p->ccp_rtcode, &service_rc);
            if (service_rc == OK) {
              src_p = (UCHAR *)cprb_p + sizeof(struct z90c_cprb);
              src_p += 4;
              le2toI(src_p, &src_l); 
              src_l -= 2;            
              src_p += 2;            
            }
            else {
              le2toI(cprb_p->ccp_rscode, &service_rs);
              PDEBUG("service rc:%d; service rs:%d\n",service_rc,service_rs);
              rv = 8;
	    }
	  }
	  else {  
            cprbx_p = (Z90C_CPRBX *) cprb_p;
	    service_rc = (int)(cprbx_p->ccp_rtcode);
            if (service_rc == OK) {
              src_p = (UCHAR *)cprbx_p + sizeof(struct z90c_cprbx);
              src_p += 4;
	      src_l = (int)(*((short *)src_p)); 
              src_l -= 2;            
              src_p += 2;            
            }
            else {
	      service_rs = (int)(cprbx_p->ccp_rscode);
              PRINTK("service rc:%d; service rs:%d\n",service_rc,service_rs);
              rv = 8;
	    }
	  }
        }
        else                           
          rv = 4;                      
      } 
      else                             
        rv = 4;                        
      break;
    default:
      break;
  }
  if (rv == OK)
    if (service_rc == OK)
      do {
        if (src_l > icaMsg_p->outputdatalength){
          rv = REC_OPERAND_SIZE;
          break;
        }
        if (src_l > RESPBUFFSIZE) {
          rv = REC_OPERAND_SIZE;
          break;
        }
        if (src_l <= 0) {
          rv = REC_OPERAND_SIZE;
          break;
        }
        PDEBUG("Length returned in convert response: %d\n",src_l);
        tgt_p = respbuff + icaMsg_p->outputdatalength - src_l;
        memcpy(tgt_p, src_p, src_l);
        if ((t82h_p->z90c_type82_hdr_type == Z90C_TYPE86_RSP_CODE) &&
            (respbuff < tgt_p)) {
          memset(respbuff, 0, icaMsg_p->outputdatalength - src_l);
          if ((rv = pad_msg(respbuff, icaMsg_p->outputdatalength, src_l)) != OK)
            break;
        }
        *respbufflen_p = icaMsg_p->outputdatalength;
        if (*respbufflen_p ==  0)
          PRINTK("Zero *respbufflen_p in convert_response\n");
      } while(0);
    else
      rv = REC_OPERAND_INVALID;
  else
    if (rv == 4)
      switch (reply_code) {
        case REPLY_ERROR_OPERAND_INVALID:
          rv = REC_OPERAND_INVALID;
          break;
        case REPLY_ERROR_OPERAND_SIZE:
          rv = REC_OPERAND_SIZE;
          break;
        case REPLY_ERROR_EVEN_MOD_IN_OPND:
          rv = REC_EVEN_MODULUS;
          break;
        case REPLY_ERROR_MESSAGE_TYPE:
          rv = WRONG_DEVICE_TYPE;
          break;
        default:
          rv = 12;
          break;
      }
  return (rv);
} 
