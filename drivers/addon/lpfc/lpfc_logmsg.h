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
 * $Id: lpfc_logmsg.h 369 2005-07-08 23:29:48Z sf_support $
 */

#ifndef _H_LPFC_LOGMSG
#define _H_LPFC_LOGMSG

/*
 * Log Message Structure
 *
 * The following structure supports LOG messages only.
 * Every LOG message is associated to a msgBlkLogDef structure of the 
 * following type.
 */

typedef struct msgLogType {
	int msgNum;		/* Message number */
	char *msgStr;		/* Ptr to log message */
	char *msgPreambleStr;	/* Ptr to log message preamble */
	int msgOutput;		/* Message output target - bitmap */
	/*
	 * This member controls message OUTPUT.
	 *
	 * The phase 'global controls' refers to user configurable parameters
	 * such as LOG_VERBOSE that control message output on a global basis.
	 */

#define LPFC_MSG_OPUT_GLOB_CTRL         0x0	/* Use global control */
#define LPFC_MSG_OPUT_DISA              0x1	/* Override global control */
#define LPFC_MSG_OPUT_FORCE             0x2	/* Override global control */
	int msgType;		/* Message LOG type - bitmap */
#define LPFC_LOG_MSG_TYPE_INFO          0x1	/* Maskable */
#define LPFC_LOG_MSG_TYPE_WARN          0x2	/* Non-Maskable */
#define LPFC_LOG_MSG_TYPE_ERR_CFG       0x4	/* Non-Maskable */
#define LPFC_LOG_MSG_TYPE_ERR           0x8	/* Non-Maskable */
#define LPFC_LOG_MSG_TYPE_PANIC        0x10	/* Non-Maskable */
	int msgMask;		/* Message LOG mask - bitmap */
	/*
	 * NOTE: Only LOG messages of types MSG_TYPE_WARN & MSG_TYPE_INFO are 
	 * maskable at the GLOBAL level.
	 * 
	 * Any LOG message regardless of message type can be disabled (override
	 * verbose) at the msgBlkLogDef struct level my setting member msgOutput
	 * = LPFC_MSG_OPUT_DISA.  The message will never be displayed regardless
	 * of verbose mask.
	 * 
	 * Any LOG message regardless of message type can be enable (override
	 * verbose) at the msgBlkLogDef struct level my setting member msgOutput
	 * = LPFC_MSG_OPUT_FORCE.  The message will always be displayed
	 * regardless of verbose mask.
	 */
#define LOG_ELS                       0x1	/* ELS events */
#define LOG_DISCOVERY                 0x2	/* Link discovery events */
#define LOG_MBOX                      0x4	/* Mailbox events */
#define LOG_INIT                      0x8	/* Initialization events */
#define LOG_LINK_EVENT                0x10	/* Link events */
#define LOG_IP                        0x20	/* IP traffic history */
#define LOG_FCP                       0x40	/* FCP traffic history */
#define LOG_NODE                      0x80	/* Node table events */
#define LOG_MISC                      0x400	/* Miscellaneous events */
#define LOG_SLI                       0x800	/* SLI events */
#define LOG_CHK_COND                  0x1000	/* FCP Check condition flag */
#define LOG_LIBDFC                    0x2000	/* Libdfc events */
#define LOG_ALL_MSG                   0xffff	/* LOG all messages */

	unsigned int msgAuxLogID;	/* Message LOG ID - This auxilliary
					   member describes the failure. */
#define ERRID_LOG_TIMEOUT             0xfdefefa7 /* Fibre Channel timeout */
#define ERRID_LOG_HDW_ERR             0x1ae4fffc /* Fibre Channel hardware
						    failure */
#define ERRID_LOG_UNEXPECT_EVENT      0xbdb7e728 /* Fibre Channel unexpected
						    event */
#define ERRID_LOG_INIT                0xbe1043b8 /* Fibre Channel init
						    failure */
#define ERRID_LOG_NO_RESOURCE         0x474c1775 /* Fibre Channel no
						    resources */
} msgLogDef;

/*
 * Message logging function prototypes
 */

int lpfc_log_chk_msg_disabled(int, msgLogDef *);
int lpfc_printf_log(int, msgLogDef *, void *, ...);
int lpfc_printf_log_msgblk(int, msgLogDef *, char *);

/*
 * External Declarations for LOG Messages
 */

/* ELS LOG Messages */
extern char lpfc_mes0100[];
extern char lpfc_mes0101[];
extern char lpfc_mes0102[];
extern char lpfc_mes0103[];
extern char lpfc_mes0104[];
extern char lpfc_mes0105[];
extern char lpfc_mes0106[];
extern char lpfc_mes0107[];
extern char lpfc_mes0108[];
extern char lpfc_mes0109[];
extern char lpfc_mes0110[];
extern char lpfc_mes0111[];
extern char lpfc_mes0112[];
extern char lpfc_mes0113[];
extern char lpfc_mes0114[];
extern char lpfc_mes0115[];
extern char lpfc_mes0116[];
extern char lpfc_mes0117[];
extern char lpfc_mes0118[];
extern char lpfc_mes0119[];
extern char lpfc_mes0120[];
extern char lpfc_mes0121[];
extern char lpfc_mes0122[];
extern char lpfc_mes0123[];
extern char lpfc_mes0124[];
extern char lpfc_mes0125[];
extern char lpfc_mes0126[];
extern char lpfc_mes0127[];

/* DISCOVERY LOG Messages */
extern char lpfc_mes0200[];
extern char lpfc_mes0201[];
extern char lpfc_mes0202[];
extern char lpfc_mes0204[];
extern char lpfc_mes0205[];
extern char lpfc_mes0206[];
extern char lpfc_mes0207[];
extern char lpfc_mes0208[];
extern char lpfc_mes0209[];
extern char lpfc_mes0210[];
extern char lpfc_mes0211[];
extern char lpfc_mes0212[];
extern char lpfc_mes0213[];
extern char lpfc_mes0214[];
extern char lpfc_mes0215[];
extern char lpfc_mes0216[];
extern char lpfc_mes0217[];
extern char lpfc_mes0218[];
extern char lpfc_mes0219[];
extern char lpfc_mes0220[];
extern char lpfc_mes0221[];
extern char lpfc_mes0222[];
extern char lpfc_mes0223[];
extern char lpfc_mes0224[];
extern char lpfc_mes0225[];
extern char lpfc_mes0226[];
extern char lpfc_mes0227[];
extern char lpfc_mes0228[];
extern char lpfc_mes0229[];
extern char lpfc_mes0230[];
extern char lpfc_mes0231[];
extern char lpfc_mes0232[];
extern char lpfc_mes0234[];
extern char lpfc_mes0235[];
extern char lpfc_mes0236[];
extern char lpfc_mes0237[];
extern char lpfc_mes0238[];
extern char lpfc_mes0239[];
extern char lpfc_mes0240[];
extern char lpfc_mes0241[];
extern char lpfc_mes0242[];
extern char lpfc_mes0243[];
extern char lpfc_mes0244[];
extern char lpfc_mes0245[];
extern char lpfc_mes0246[];
extern char lpfc_mes0247[];
extern char lpfc_mes0248[];
extern char lpfc_mes0249[];
extern char lpfc_mes0250[];
extern char lpfc_mes0256[];
extern char lpfc_mes0260[];
extern char lpfc_mes0261[];

/* MAILBOX LOG Messages */
extern char lpfc_mes0300[];
extern char lpfc_mes0301[];
extern char lpfc_mes0302[];
extern char lpfc_mes0304[];
extern char lpfc_mes0305[];
extern char lpfc_mes0306[];
extern char lpfc_mes0307[];
extern char lpfc_mes0308[];
extern char lpfc_mes0309[];
extern char lpfc_mes0310[];
extern char lpfc_mes0311[];
extern char lpfc_mes0312[];
extern char lpfc_mes0313[];
extern char lpfc_mes0314[];
extern char lpfc_mes0315[];
extern char lpfc_mes0316[];
extern char lpfc_mes0317[];
extern char lpfc_mes0318[];
extern char lpfc_mes0319[];
extern char lpfc_mes0320[];
extern char lpfc_mes0321[];
extern char lpfc_mes0322[];
extern char lpfc_mes0323[];
extern char lpfc_mes0324[];
extern char lpfc_mes0325[];
extern char lpfc_mes0326[];

/* INIT LOG Messages */
extern char lpfc_mes0405[];
extern char lpfc_mes0410[];
extern char lpfc_mes0411[];
extern char lpfc_mes0412[];
extern char lpfc_mes0413[];
extern char lpfc_mes0430[];
extern char lpfc_mes0431[];
extern char lpfc_mes0432[];
extern char lpfc_mes0433[];
extern char lpfc_mes0434[];
extern char lpfc_mes0435[];
extern char lpfc_mes0436[];
extern char lpfc_mes0437[];
extern char lpfc_mes0438[];
extern char lpfc_mes0439[];
extern char lpfc_mes0440[];
extern char lpfc_mes0441[];
extern char lpfc_mes0442[];
extern char lpfc_mes0443[];
extern char lpfc_mes0446[];
extern char lpfc_mes0447[];
extern char lpfc_mes0448[];
extern char lpfc_mes0451[];
extern char lpfc_mes0453[];
extern char lpfc_mes0454[];
extern char lpfc_mes0455[];
extern char lpfc_mes0457[];
extern char lpfc_mes0458[];
extern char lpfc_mes0460[];
extern char lpfc_mes0462[];

/* IP LOG Messages */
extern char lpfc_mes0600[];
extern char lpfc_mes0601[];
extern char lpfc_mes0610[];

/* FCP LOG Messages */
extern char lpfc_mes0701[];
extern char lpfc_mes0702[];
extern char lpfc_mes0703[];
extern char lpfc_mes0712[];
extern char lpfc_mes0713[];
extern char lpfc_mes0714[];
extern char lpfc_mes0716[];
extern char lpfc_mes0717[];
extern char lpfc_mes0729[];
extern char lpfc_mes0730[];
extern char lpfc_mes0734[];
extern char lpfc_mes0735[];
extern char lpfc_mes0737[];
extern char lpfc_mes0738[];
extern char lpfc_mes0747[];
extern char lpfc_mes0748[];
extern char lpfc_mes0749[];
extern char lpfc_mes0754[];

/* NODE LOG Messages */
extern char lpfc_mes0900[];
extern char lpfc_mes0901[];
extern char lpfc_mes0902[];
extern char lpfc_mes0903[];
extern char lpfc_mes0904[];
extern char lpfc_mes0905[];
extern char lpfc_mes0906[];
extern char lpfc_mes0907[];
extern char lpfc_mes0908[];
extern char lpfc_mes0910[];
extern char lpfc_mes0911[];
extern char lpfc_mes0929[];
extern char lpfc_mes0930[];
extern char lpfc_mes0931[];
extern char lpfc_mes0932[];

/* MISC LOG messages */
extern char lpfc_mes1208[];
extern char lpfc_mes1210[];
extern char lpfc_mes1212[];
extern char lpfc_mes1213[];

/* LINK LOG Messages */
extern char lpfc_mes1300[];
extern char lpfc_mes1301[];
extern char lpfc_mes1302[];
extern char lpfc_mes1303[];
extern char lpfc_mes1304[];
extern char lpfc_mes1305[];
extern char lpfc_mes1306[];
extern char lpfc_mes1307[];

/* CHK CONDITION LOG Messages */

/* Libdfc Log Messages */
extern char lpfc_mes1600[];
extern char lpfc_mes1601[];
extern char lpfc_mes1602[];
extern char lpfc_mes1603[];
extern char lpfc_mes1604[];
extern char lpfc_mes1606[];
extern char lpfc_mes1607[];

/*
 * External Declarations for LOG Message Structure msgBlkLogDef
 */

/* ELS LOG Message Structures */
extern msgLogDef lpfc_msgBlk0100;
extern msgLogDef lpfc_msgBlk0101;
extern msgLogDef lpfc_msgBlk0102;
extern msgLogDef lpfc_msgBlk0103;
extern msgLogDef lpfc_msgBlk0104;
extern msgLogDef lpfc_msgBlk0105;
extern msgLogDef lpfc_msgBlk0106;
extern msgLogDef lpfc_msgBlk0107;
extern msgLogDef lpfc_msgBlk0108;
extern msgLogDef lpfc_msgBlk0109;
extern msgLogDef lpfc_msgBlk0110;
extern msgLogDef lpfc_msgBlk0111;
extern msgLogDef lpfc_msgBlk0112;
extern msgLogDef lpfc_msgBlk0113;
extern msgLogDef lpfc_msgBlk0114;
extern msgLogDef lpfc_msgBlk0115;
extern msgLogDef lpfc_msgBlk0116;
extern msgLogDef lpfc_msgBlk0117;
extern msgLogDef lpfc_msgBlk0118;
extern msgLogDef lpfc_msgBlk0119;
extern msgLogDef lpfc_msgBlk0120;
extern msgLogDef lpfc_msgBlk0121;
extern msgLogDef lpfc_msgBlk0122;
extern msgLogDef lpfc_msgBlk0123;
extern msgLogDef lpfc_msgBlk0124;
extern msgLogDef lpfc_msgBlk0125;
extern msgLogDef lpfc_msgBlk0126;
extern msgLogDef lpfc_msgBlk0127;

/* DISCOVERY LOG Message Structures */
extern msgLogDef lpfc_msgBlk0200;
extern msgLogDef lpfc_msgBlk0201;
extern msgLogDef lpfc_msgBlk0202;
extern msgLogDef lpfc_msgBlk0204;
extern msgLogDef lpfc_msgBlk0205;
extern msgLogDef lpfc_msgBlk0206;
extern msgLogDef lpfc_msgBlk0207;
extern msgLogDef lpfc_msgBlk0208;
extern msgLogDef lpfc_msgBlk0209;
extern msgLogDef lpfc_msgBlk0210;
extern msgLogDef lpfc_msgBlk0211;
extern msgLogDef lpfc_msgBlk0212;
extern msgLogDef lpfc_msgBlk0213;
extern msgLogDef lpfc_msgBlk0214;
extern msgLogDef lpfc_msgBlk0215;
extern msgLogDef lpfc_msgBlk0216;
extern msgLogDef lpfc_msgBlk0217;
extern msgLogDef lpfc_msgBlk0218;
extern msgLogDef lpfc_msgBlk0219;
extern msgLogDef lpfc_msgBlk0220;
extern msgLogDef lpfc_msgBlk0221;
extern msgLogDef lpfc_msgBlk0222;
extern msgLogDef lpfc_msgBlk0223;
extern msgLogDef lpfc_msgBlk0224;
extern msgLogDef lpfc_msgBlk0225;
extern msgLogDef lpfc_msgBlk0226;
extern msgLogDef lpfc_msgBlk0227;
extern msgLogDef lpfc_msgBlk0228;
extern msgLogDef lpfc_msgBlk0229;
extern msgLogDef lpfc_msgBlk0230;
extern msgLogDef lpfc_msgBlk0231;
extern msgLogDef lpfc_msgBlk0232;
extern msgLogDef lpfc_msgBlk0234;
extern msgLogDef lpfc_msgBlk0235;
extern msgLogDef lpfc_msgBlk0236;
extern msgLogDef lpfc_msgBlk0237;
extern msgLogDef lpfc_msgBlk0238;
extern msgLogDef lpfc_msgBlk0239;
extern msgLogDef lpfc_msgBlk0240;
extern msgLogDef lpfc_msgBlk0241;
extern msgLogDef lpfc_msgBlk0242;
extern msgLogDef lpfc_msgBlk0243;
extern msgLogDef lpfc_msgBlk0244;
extern msgLogDef lpfc_msgBlk0245;
extern msgLogDef lpfc_msgBlk0246;
extern msgLogDef lpfc_msgBlk0247;
extern msgLogDef lpfc_msgBlk0248;
extern msgLogDef lpfc_msgBlk0249;
extern msgLogDef lpfc_msgBlk0250;
extern msgLogDef lpfc_msgBlk0256;
extern msgLogDef lpfc_msgBlk0260;
extern msgLogDef lpfc_msgBlk0261;


/* MAILBOX LOG Message Structures */
extern msgLogDef lpfc_msgBlk0300;
extern msgLogDef lpfc_msgBlk0301;
extern msgLogDef lpfc_msgBlk0302;
extern msgLogDef lpfc_msgBlk0304;
extern msgLogDef lpfc_msgBlk0305;
extern msgLogDef lpfc_msgBlk0306;
extern msgLogDef lpfc_msgBlk0307;
extern msgLogDef lpfc_msgBlk0308;
extern msgLogDef lpfc_msgBlk0309;
extern msgLogDef lpfc_msgBlk0310;
extern msgLogDef lpfc_msgBlk0311;
extern msgLogDef lpfc_msgBlk0312;
extern msgLogDef lpfc_msgBlk0313;
extern msgLogDef lpfc_msgBlk0314;
extern msgLogDef lpfc_msgBlk0315;
extern msgLogDef lpfc_msgBlk0316;
extern msgLogDef lpfc_msgBlk0317;
extern msgLogDef lpfc_msgBlk0318;
extern msgLogDef lpfc_msgBlk0319;
extern msgLogDef lpfc_msgBlk0320;
extern msgLogDef lpfc_msgBlk0321;
extern msgLogDef lpfc_msgBlk0322;
extern msgLogDef lpfc_msgBlk0323;
extern msgLogDef lpfc_msgBlk0324;
extern msgLogDef lpfc_msgBlk0325;
extern msgLogDef lpfc_msgBlk0326;

/* INIT LOG Message Structures */
extern msgLogDef lpfc_msgBlk0405;
extern msgLogDef lpfc_msgBlk0410;
extern msgLogDef lpfc_msgBlk0411;
extern msgLogDef lpfc_msgBlk0412;
extern msgLogDef lpfc_msgBlk0413;
extern msgLogDef lpfc_msgBlk0430;
extern msgLogDef lpfc_msgBlk0431;
extern msgLogDef lpfc_msgBlk0432;
extern msgLogDef lpfc_msgBlk0433;
extern msgLogDef lpfc_msgBlk0434;
extern msgLogDef lpfc_msgBlk0435;
extern msgLogDef lpfc_msgBlk0436;
extern msgLogDef lpfc_msgBlk0437;
extern msgLogDef lpfc_msgBlk0438;
extern msgLogDef lpfc_msgBlk0439;
extern msgLogDef lpfc_msgBlk0440;
extern msgLogDef lpfc_msgBlk0441;
extern msgLogDef lpfc_msgBlk0442;
extern msgLogDef lpfc_msgBlk0443;
extern msgLogDef lpfc_msgBlk0446;
extern msgLogDef lpfc_msgBlk0447;
extern msgLogDef lpfc_msgBlk0448;
extern msgLogDef lpfc_msgBlk0451;
extern msgLogDef lpfc_msgBlk0453;
extern msgLogDef lpfc_msgBlk0454;
extern msgLogDef lpfc_msgBlk0455;
extern msgLogDef lpfc_msgBlk0457;
extern msgLogDef lpfc_msgBlk0458;
extern msgLogDef lpfc_msgBlk0460;
extern msgLogDef lpfc_msgBlk0462;

/* IP LOG Message Structures */
extern msgLogDef lpfc_msgBlk0600;
extern msgLogDef lpfc_msgBlk0601;
extern msgLogDef lpfc_msgBlk0610;

/* FCP LOG Message Structures */
extern msgLogDef lpfc_msgBlk0701;
extern msgLogDef lpfc_msgBlk0702;
extern msgLogDef lpfc_msgBlk0703;
extern msgLogDef lpfc_msgBlk0712;
extern msgLogDef lpfc_msgBlk0713;
extern msgLogDef lpfc_msgBlk0714;
extern msgLogDef lpfc_msgBlk0716;
extern msgLogDef lpfc_msgBlk0717;
extern msgLogDef lpfc_msgBlk0729;
extern msgLogDef lpfc_msgBlk0730;
extern msgLogDef lpfc_msgBlk0734;
extern msgLogDef lpfc_msgBlk0735;
extern msgLogDef lpfc_msgBlk0737;
extern msgLogDef lpfc_msgBlk0738;
extern msgLogDef lpfc_msgBlk0747;
extern msgLogDef lpfc_msgBlk0748;
extern msgLogDef lpfc_msgBlk0749;
extern msgLogDef lpfc_msgBlk0754;

/* NODE LOG Message Structures */
extern msgLogDef lpfc_msgBlk0900;
extern msgLogDef lpfc_msgBlk0901;
extern msgLogDef lpfc_msgBlk0902;
extern msgLogDef lpfc_msgBlk0903;
extern msgLogDef lpfc_msgBlk0904;
extern msgLogDef lpfc_msgBlk0905;
extern msgLogDef lpfc_msgBlk0906;
extern msgLogDef lpfc_msgBlk0907;
extern msgLogDef lpfc_msgBlk0908;
extern msgLogDef lpfc_msgBlk0910;
extern msgLogDef lpfc_msgBlk0911;
extern msgLogDef lpfc_msgBlk0929;
extern msgLogDef lpfc_msgBlk0930;
extern msgLogDef lpfc_msgBlk0931;
extern msgLogDef lpfc_msgBlk0932;

/* MISC LOG Message Structures */
extern msgLogDef lpfc_msgBlk1208;
extern msgLogDef lpfc_msgBlk1210;
extern msgLogDef lpfc_msgBlk1212;
extern msgLogDef lpfc_msgBlk1213;

/* LINK LOG Message Structures */
extern msgLogDef lpfc_msgBlk1300;
extern msgLogDef lpfc_msgBlk1301;
extern msgLogDef lpfc_msgBlk1302;
extern msgLogDef lpfc_msgBlk1303;
extern msgLogDef lpfc_msgBlk1304;
extern msgLogDef lpfc_msgBlk1305;
extern msgLogDef lpfc_msgBlk1306;
extern msgLogDef lpfc_msgBlk1307;

/* CHK CONDITION LOG Message Structures */

/* Libdfc LOG Message Structures */
extern msgLogDef lpfc_msgBlk1600;
extern msgLogDef lpfc_msgBlk1601;
extern msgLogDef lpfc_msgBlk1602;
extern msgLogDef lpfc_msgBlk1603;
extern msgLogDef lpfc_msgBlk1604;
extern msgLogDef lpfc_msgBlk1606;
extern msgLogDef lpfc_msgBlk1607;

/* 
 * LOG Messages Numbers
 */

/* ELS LOG Message Numbers */
#define LPFC_LOG_MSG_EL_0100    100
#define LPFC_LOG_MSG_EL_0101    101
#define LPFC_LOG_MSG_EL_0102    102
#define LPFC_LOG_MSG_EL_0103    103
#define LPFC_LOG_MSG_EL_0104    104
#define LPFC_LOG_MSG_EL_0105    105
#define LPFC_LOG_MSG_EL_0106    106
#define LPFC_LOG_MSG_EL_0107    107
#define LPFC_LOG_MSG_EL_0108    108
#define LPFC_LOG_MSG_EL_0109    109
#define LPFC_LOG_MSG_EL_0110    110
#define LPFC_LOG_MSG_EL_0111    111
#define LPFC_LOG_MSG_EL_0112    112
#define LPFC_LOG_MSG_EL_0113    113
#define LPFC_LOG_MSG_EL_0114    114
#define LPFC_LOG_MSG_EL_0115    115
#define LPFC_LOG_MSG_EL_0116    116
#define LPFC_LOG_MSG_EL_0117    117
#define LPFC_LOG_MSG_EL_0118    118
#define LPFC_LOG_MSG_EL_0119    119
#define LPFC_LOG_MSG_EL_0120    120
#define LPFC_LOG_MSG_EL_0121    121
#define LPFC_LOG_MSG_EL_0122    122
#define LPFC_LOG_MSG_EL_0123    123
#define LPFC_LOG_MSG_EL_0124    124
#define LPFC_LOG_MSG_EL_0125    125
#define LPFC_LOG_MSG_EL_0126    126
#define LPFC_LOG_MSG_EL_0127    127

/* DISCOVERY LOG Message Numbers */
#define LPFC_LOG_MSG_DI_0200    200
#define LPFC_LOG_MSG_DI_0201    201
#define LPFC_LOG_MSG_DI_0202    202
#define LPFC_LOG_MSG_DI_0204    204
#define LPFC_LOG_MSG_DI_0205    205
#define LPFC_LOG_MSG_DI_0206    206
#define LPFC_LOG_MSG_DI_0207    207
#define LPFC_LOG_MSG_DI_0208    208
#define LPFC_LOG_MSG_DI_0209    209
#define LPFC_LOG_MSG_DI_0210    210
#define LPFC_LOG_MSG_DI_0211    211
#define LPFC_LOG_MSG_DI_0212    212
#define LPFC_LOG_MSG_DI_0213    213
#define LPFC_LOG_MSG_DI_0214    214
#define LPFC_LOG_MSG_DI_0215    215
#define LPFC_LOG_MSG_DI_0216    216
#define LPFC_LOG_MSG_DI_0217    217
#define LPFC_LOG_MSG_DI_0218    218
#define LPFC_LOG_MSG_DI_0219    219
#define LPFC_LOG_MSG_DI_0220    220
#define LPFC_LOG_MSG_DI_0221    221
#define LPFC_LOG_MSG_DI_0222    222
#define LPFC_LOG_MSG_DI_0223    223
#define LPFC_LOG_MSG_DI_0224    224
#define LPFC_LOG_MSG_DI_0225    225
#define LPFC_LOG_MSG_DI_0226    226
#define LPFC_LOG_MSG_DI_0227    227
#define LPFC_LOG_MSG_DI_0228    228
#define LPFC_LOG_MSG_DI_0229    229
#define LPFC_LOG_MSG_DI_0230    230
#define LPFC_LOG_MSG_DI_0231    231
#define LPFC_LOG_MSG_DI_0232    232
#define LPFC_LOG_MSG_DI_0234    234
#define LPFC_LOG_MSG_DI_0235    235
#define LPFC_LOG_MSG_DI_0236    236
#define LPFC_LOG_MSG_DI_0237    237
#define LPFC_LOG_MSG_DI_0238    238
#define LPFC_LOG_MSG_DI_0239    239
#define LPFC_LOG_MSG_DI_0240    240
#define LPFC_LOG_MSG_DI_0241    241
#define LPFC_LOG_MSG_DI_0242    242
#define LPFC_LOG_MSG_DI_0243    243
#define LPFC_LOG_MSG_DI_0244    244
#define LPFC_LOG_MSG_DI_0245    245
#define LPFC_LOG_MSG_DI_0246    246
#define LPFC_LOG_MSG_DI_0247    247
#define LPFC_LOG_MSG_DI_0248    248
#define LPFC_LOG_MSG_DI_0249    249
#define LPFC_LOG_MSG_DI_0250    250
#define LPFC_LOG_MSG_DI_0256    256
#define LPFC_LOG_MSG_DI_0260    260
#define LPFC_LOG_MSG_DI_0261    261

/* MAILBOX LOG Message Numbers */
#define LPFC_LOG_MSG_MB_0300    300
#define LPFC_LOG_MSG_MB_0301    301
#define LPFC_LOG_MSG_MB_0302    302
#define LPFC_LOG_MSG_MB_0304    304
#define LPFC_LOG_MSG_MB_0305    305
#define LPFC_LOG_MSG_MB_0306    306
#define LPFC_LOG_MSG_MB_0307    307
#define LPFC_LOG_MSG_MB_0308    308
#define LPFC_LOG_MSG_MB_0309    309
#define LPFC_LOG_MSG_MB_0310    310
#define LPFC_LOG_MSG_MB_0311    311
#define LPFC_LOG_MSG_MB_0312    312
#define LPFC_LOG_MSG_MB_0313    313
#define LPFC_LOG_MSG_MB_0314    314
#define LPFC_LOG_MSG_MB_0315    315
#define LPFC_LOG_MSG_MB_0316    316
#define LPFC_LOG_MSG_MB_0317    317
#define LPFC_LOG_MSG_MB_0318    318
#define LPFC_LOG_MSG_MB_0319    319
#define LPFC_LOG_MSG_MB_0320    320
#define LPFC_LOG_MSG_MB_0321    321
#define LPFC_LOG_MSG_MB_0322    322
#define LPFC_LOG_MSG_MB_0323    323
#define LPFC_LOG_MSG_MB_0324    324
#define LPFC_LOG_MSG_MB_0325    325
#define LPFC_LOG_MSG_MB_0326    326

/* INIT LOG Message Numbers */
#define LPFC_LOG_MSG_IN_0405    405
#define LPFC_LOG_MSG_IN_0410    410
#define LPFC_LOG_MSG_IN_0411    411
#define LPFC_LOG_MSG_IN_0412    412
#define LPFC_LOG_MSG_IN_0413    413
#define LPFC_LOG_MSG_IN_0430    430
#define LPFC_LOG_MSG_IN_0431    431
#define LPFC_LOG_MSG_IN_0432    432
#define LPFC_LOG_MSG_IN_0433    433
#define LPFC_LOG_MSG_IN_0434    434
#define LPFC_LOG_MSG_IN_0435    435
#define LPFC_LOG_MSG_IN_0436    436
#define LPFC_LOG_MSG_IN_0437    437
#define LPFC_LOG_MSG_IN_0438    438
#define LPFC_LOG_MSG_IN_0439    439
#define LPFC_LOG_MSG_IN_0440    440
#define LPFC_LOG_MSG_IN_0441    441
#define LPFC_LOG_MSG_IN_0442    442
#define LPFC_LOG_MSG_IN_0443    443
#define LPFC_LOG_MSG_IN_0446    446
#define LPFC_LOG_MSG_IN_0447    447
#define LPFC_LOG_MSG_IN_0448    448
#define LPFC_LOG_MSG_IN_0451    451
#define LPFC_LOG_MSG_IN_0453    453
#define LPFC_LOG_MSG_IN_0454    454
#define LPFC_LOG_MSG_IN_0455    455
#define LPFC_LOG_MSG_IN_0457    457
#define LPFC_LOG_MSG_IN_0458    458
#define LPFC_LOG_MSG_IN_0460    460
#define LPFC_LOG_MSG_IN_0462    462

/*
 * Available.LPFC_LOG_MSG_IN_0500    500
 */

/* IP LOG Message Numbers */
#define LPFC_LOG_MSG_IP_0600    600
#define LPFC_LOG_MSG_IP_0601    601
#define LPFC_LOG_MSG_IP_0610    610

/* FCP LOG Message Numbers */
#define LPFC_LOG_MSG_FP_0701    701
#define LPFC_LOG_MSG_FP_0702    702
#define LPFC_LOG_MSG_FP_0703    703
#define LPFC_LOG_MSG_FP_0712    712
#define LPFC_LOG_MSG_FP_0713    713
#define LPFC_LOG_MSG_FP_0714    714
#define LPFC_LOG_MSG_FP_0716    716
#define LPFC_LOG_MSG_FP_0717    717
#define LPFC_LOG_MSG_FP_0729    729
#define LPFC_LOG_MSG_FP_0730    730
#define LPFC_LOG_MSG_FP_0734    734
#define LPFC_LOG_MSG_FP_0735    735
#define LPFC_LOG_MSG_FP_0737    737
#define LPFC_LOG_MSG_FP_0738    738
#define LPFC_LOG_MSG_FP_0747    747
#define LPFC_LOG_MSG_FP_0748    748
#define LPFC_LOG_MSG_FP_0749    749
#define LPFC_LOG_MSG_FP_0754    754

/*
 * Available:  LPFC_LOG_MSG_FP_0800    800
 */

/* NODE LOG Message Numbers */
#define LPFC_LOG_MSG_ND_0900    900
#define LPFC_LOG_MSG_ND_0901    901
#define LPFC_LOG_MSG_ND_0902    902
#define LPFC_LOG_MSG_ND_0903    903
#define LPFC_LOG_MSG_ND_0904    904
#define LPFC_LOG_MSG_ND_0905    905
#define LPFC_LOG_MSG_ND_0906    906
#define LPFC_LOG_MSG_ND_0907    907
#define LPFC_LOG_MSG_ND_0908    908
#define LPFC_LOG_MSG_ND_0910    910
#define LPFC_LOG_MSG_ND_0911    911
#define LPFC_LOG_MSG_ND_0929    929
#define LPFC_LOG_MSG_ND_0930    930
#define LPFC_LOG_MSG_ND_0931    931
#define LPFC_LOG_MSG_ND_0932    932

/* MISC LOG Message Numbers */
#define LPFC_LOG_MSG_MI_1208   1208
#define LPFC_LOG_MSG_MI_1210   1210
#define LPFC_LOG_MSG_MI_1212   1212
#define LPFC_LOG_MSG_MI_1213   1213

/* LINK LOG Message Numbers */
#define LPFC_LOG_MSG_LK_1300   1300
#define LPFC_LOG_MSG_LK_1301   1301
#define LPFC_LOG_MSG_LK_1302   1302
#define LPFC_LOG_MSG_LK_1303   1303
#define LPFC_LOG_MSG_LK_1304   1304
#define LPFC_LOG_MSG_LK_1305   1305
#define LPFC_LOG_MSG_LK_1306   1306
#define LPFC_LOG_MSG_LK_1307   1307

/* CHK COMDITION LOG Message Numbers */
/*
 * Available LPFC_LOG_MSG_LK_1500   1500
 */

/* Libdfc LOG Message Numbers */
#define LPFC_LOG_MSG_IO_1600   1600
#define LPFC_LOG_MSG_IO_1601   1601
#define LPFC_LOG_MSG_IO_1602   1602
#define LPFC_LOG_MSG_IO_1603   1603
#define LPFC_LOG_MSG_IO_1604   1604
#define LPFC_LOG_MSG_IO_1606   1606
#define LPFC_LOG_MSG_IO_1607   1607

#endif				/* _H_LPFC_LOGMSG */
