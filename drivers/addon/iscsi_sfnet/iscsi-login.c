/*
 * iSCSI login library
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi-devel@lists.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi-login.c,v 1.7.2.2 2004/09/21 09:00:28 krishmnc Exp $
 *
 */

#include "iscsi-platform.h"
#include "iscsi-protocol.h"
#include "iscsi-io.h"
#include "iscsi-login.h"

struct IscsiHdr *
iscsi_align_pdu(iscsi_session_t * session, unsigned char *buffer,
		int buffersize)
{
    struct IscsiHdr *header;
    unsigned long addr = (unsigned long) buffer;

    /* find a buffer location guaranteed to be reasonably aligned for the header */
    addr += (addr % sizeof (*header));
    header = (struct IscsiHdr *) addr;

    return header;
}

/* caller is assumed to be well-behaved and passing NUL terminated strings */
int
iscsi_add_text(iscsi_session_t * session, struct IscsiHdr *pdu, char *data,
	       int max_data_length, char *param, char *value)
{
    int param_len = strlen(param);
    int value_len = strlen(value);
    int length = param_len + 1 + value_len + 1;	/* param, separator, value,
						 * and trailing NUL 
						 */
    int pdu_length = ntoh24(pdu->dlength);
    char *text = data;
    char *end = data + max_data_length;
    char *pdu_text;

    /* find the end of the current text */
    text += pdu_length;
    pdu_text = text;
    pdu_length += length;

    if (text + length >= end) {
	logmsg(AS_NOTICE, "failed to add login text '%s=%s'\n", param, value);
	return 0;
    }

    /* param */
    iscsi_strncpy(text, param, param_len);
    text += param_len;

    /* separator */
    *text++ = ISCSI_TEXT_SEPARATOR;

    /* value */
    strncpy(text, value, value_len);
    text += value_len;

    /* NUL */
    *text++ = '\0';

    /* update the length in the PDU header */
    hton24(pdu->dlength, pdu_length);

    return 1;
}

int
iscsi_find_key_value(char *param, char *pdu, char *pdu_end, char **value_start,
		     char **value_end)
{
    char *str = param;
    char *text = pdu;
    char *value = NULL;

    if (value_start)
	*value_start = NULL;
    if (value_end)
	*value_end = NULL;

    /* make sure they contain the same bytes */
    while (*str) {
	if (text >= pdu_end)
	    return 0;
	if (*text == '\0')
	    return 0;
	if (*str != *text)
	    return 0;
	str++;
	text++;
    }

    if ((text >= pdu_end) || (*text == '\0') || (*text != ISCSI_TEXT_SEPARATOR)) {
	return 0;
    }

    /* find the value */
    value = text + 1;

    /* find the end of the value */
    while ((text < pdu_end) && (*text))
	text++;

    if (value_start)
	*value_start = value;
    if (value_end)
	*value_end = text;

    return 1;
}

/* 
 * This callback may be used under certain conditions when
 * authenticating a target, but I'm not sure what we need to
 * do here.
 */
static void
null_callback(void *user_handle, void *message_handle, int auth_status)
{
    debugmsg(1, "iscsi-login: null_callback(%p, %p, %d)\n", user_handle,
	     message_handle, auth_status);
}

/* this assumes the text data is always NUL terminated.  The
 * caller can always arrange for that by using a slightly
 * larger buffer than the max PDU size, and then appending a
 * NUL to the PDU.
 */
iscsi_login_status_t
iscsi_process_login_response(iscsi_session_t * session,
			     struct IscsiLoginRspHdr *login_rsp_pdu, char *data,
			     int max_data_length)
{
    IscsiAuthClient *auth_client = (session->auth_buffers
				    && session->num_auth_buffers) ?
	(IscsiAuthClient *) session->auth_buffers[0].address : NULL;
    int transit = login_rsp_pdu->flags & ISCSI_FLAG_LOGIN_TRANSIT;
    char *text = data;
    char *end;
    int pdu_current_stage = 0, pdu_next_stage = 0;

    end = text + ntoh24(login_rsp_pdu->dlength) + 1;
    if (end >= (data + max_data_length)) {
	logmsg(AS_ERROR,
	       "login failed, process_login_response buffer too small to "
	       "guarantee NUL termination\n");
	return LOGIN_FAILED;
    }
    /* guarantee a trailing NUL */
    *end = '\0';

    /* if the response status was success, sanity check the response */
    if (login_rsp_pdu->status_class == STATUS_CLASS_SUCCESS) {
	/* check the active version */
	if (login_rsp_pdu->active_version != ISCSI_DRAFT20_VERSION) {
	    logmsg(AS_ERROR,
		   "login version mismatch, received incompatible active iSCSI "
		   "version 0x%02x, expected version 0x%02x\n",
		   login_rsp_pdu->active_version, ISCSI_DRAFT20_VERSION);
	    return LOGIN_VERSION_MISMATCH;
	}

	/* make sure the current stage matches */
	pdu_current_stage =
	    (login_rsp_pdu->flags & ISCSI_FLAG_LOGIN_CURRENT_STAGE_MASK) >> 2;
	if (pdu_current_stage != session->current_stage) {
	    logmsg(AS_ERROR,
		   "received invalid login PDU, current stage mismatch, "
		   "session %d, response %d\n",
		   session->current_stage, pdu_current_stage);
	    return LOGIN_INVALID_PDU;
	}

	/* make sure that we're actually advancing if the T-bit is set */
	pdu_next_stage =
	    login_rsp_pdu->flags & ISCSI_FLAG_LOGIN_NEXT_STAGE_MASK;
	if (transit && (pdu_next_stage <= session->current_stage)) {
	    logmsg(AS_ERROR,
		   "received invalid login PDU, current stage %d, target wants to "
		   "go to stage %d, but we want to go to stage %d\n",
		   session->current_stage, pdu_next_stage, session->next_stage);
	    return LOGIN_INVALID_PDU;
	}
    }

    if (session->current_stage == ISCSI_SECURITY_NEGOTIATION_STAGE) {
	if (iscsiAuthClientRecvBegin(auth_client) != iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR,
		   "login failed because authClientRecvBegin failed\n");
	    return LOGIN_FAILED;
	}

	if (iscsiAuthClientRecvTransitBit(auth_client, transit) !=
	    iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR,
		   "login failed because authClientRecvTransitBit failed\n");
	    return LOGIN_FAILED;
	}
    }

    /* scan the text data */
  more_text:
    while (text && (text < end)) {
	char *value = NULL;
	char *value_end = NULL;

	/* skip any NULs separating each text key=value pair */
	while ((text < end) && (*text == '\0'))
	    text++;
	if (text >= end)
	    break;

	/* handle keys appropriate for each stage */
	switch (session->current_stage) {
	case ISCSI_SECURITY_NEGOTIATION_STAGE:{
		/* a few keys are possible in Security stage which the
		 * auth code doesn't care about, but which we might
		 * want to see, or at least not choke on.
		 */
		if (iscsi_find_key_value
		    ("TargetAlias", text, end, &value, &value_end)) {
		    size_t size = sizeof (session->TargetAlias);

		    if ((value_end - value) < size)
			size = value_end - value;

		    memcpy(session->TargetAlias, value, size);
		    session->TargetAlias[sizeof (session->TargetAlias) - 1] =
			'\0';
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("TargetAddress", text, end, &value, &value_end)) {
		    /* if possible, change the session's
		     * ip_address and port to the new
		     * TargetAddress 
		     */
		    if (session->update_address
			&& session->update_address(session, value)) {
			text = value_end;
		    } else {
			logmsg(AS_ERROR,
			       "login redirection failed, can't handle "
			       "redirection to %s\n", value);
			return LOGIN_REDIRECTION_FAILED;
		    }
		} else
		    if (iscsi_find_key_value
			("TargetPortalGroupTag", text, end, &value,
			 &value_end)) {
		    /* We should have already obtained this
		     * via discovery. We've already picked
		     * an isid, so the most we can do is
		     * confirm we reached the portal group
		     * we were expecting to.
		     */
		    int tag = iscsi_strtoul(value, NULL, 0);
		    if (session->portal_group_tag >= 0) {
			if (tag != session->portal_group_tag) {
			    logmsg(AS_ERROR,
				   "portal group tag mismatch, expected %u, "
				   "received %u\n",
				   session->portal_group_tag, tag);
			    return LOGIN_WRONG_PORTAL_GROUP;
			}
		    } else {
			/* we now know the tag */
			session->portal_group_tag = tag;
		    }
		    text = value_end;
		} else {
		    /* any key we don't recognize either
		     * goes to the auth code, or we choke on
		     * it 
		     */
		    int keytype = iscsiAuthKeyTypeNone;

		    while (iscsiAuthClientGetNextKeyType(&keytype) ==
			   iscsiAuthStatusNoError) {
			char *key = (char *) iscsiAuthClientGetKeyName(keytype);

			if (key
			    && iscsi_find_key_value(key, text, end, &value,
						    &value_end)) {
			    if (iscsiAuthClientRecvKeyValue
				(auth_client, keytype,
				 value) != iscsiAuthStatusNoError) {
				logmsg(AS_ERROR,
				       "login negotiation failed, can't accept %s "
				       "in security stage\n", text);
				return LOGIN_NEGOTIATION_FAILED;
			    }
			    text = value_end;
			    goto more_text;
			}
		    }

		    logmsg(AS_ERROR,
			   "login negotiation failed, can't accept %s in "
			   "security stage\n", text);
		    return LOGIN_NEGOTIATION_FAILED;
		}
		break;
	    }
	case ISCSI_OP_PARMS_NEGOTIATION_STAGE:{
		/* FIXME: they're making base64 an encoding option for
		 * all numbers in draft13, since some security
		 * protocols use large numbers, and it was somehow
		 * considered "simpler" to let them be used for any
		 * number anywhere.
		 */

		if (iscsi_find_key_value
		    ("TargetAlias", text, end, &value, &value_end)) {
		    size_t size = sizeof (session->TargetAlias);

		    if ((value_end - value) < size)
			size = value_end - value;

		    memcpy(session->TargetAlias, value, size);
		    session->TargetAlias[sizeof (session->TargetAlias) - 1] =
			'\0';
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("TargetAddress", text, end, &value, &value_end)) {
		    if (session->update_address
			&& session->update_address(session, value)) {
			text = value_end;
		    } else {
			logmsg(AS_ERROR,
			       "login redirection failed, can't handle redirection "
			       "to %s\n", value);
			return LOGIN_REDIRECTION_FAILED;
		    }
		} else
		    if (iscsi_find_key_value
			("TargetPortalGroupTag", text, end, &value,
			 &value_end)) {
		    /* We should have already obtained this
		     * via discovery. We've already picked
		     * an isid, so the most we can do is
		     * confirm we reached the portal group
		     * we were expecting to.
		     */
		    int tag = iscsi_strtoul(value, NULL, 0);
		    if (session->portal_group_tag >= 0) {
			if (tag != session->portal_group_tag) {
			    logmsg(AS_ERROR,
				   "portal group tag mismatch, expected %u, "
				   "received %u\n",
				   session->portal_group_tag, tag);
			    return LOGIN_WRONG_PORTAL_GROUP;
			}
		    } else {
			/* we now know the tag */
			session->portal_group_tag = tag;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("InitialR2T", text, end, &value, &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if (value && (iscsi_strcmp(value, "Yes") == 0))
			    session->InitialR2T = 1;
			else
			    session->InitialR2T = 0;
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_INITIALR2T;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("ImmediateData", text, end, &value, &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if (value && (iscsi_strcmp(value, "Yes") == 0))
			    session->ImmediateData = 1;
			else
			    session->ImmediateData = 0;
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_IMMEDIATEDATA;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("MaxRecvDataSegmentLength", text, end, &value,
			 &value_end)) {
		    /* FIXME: no octal */
		    session->MaxXmitDataSegmentLength =
			iscsi_strtoul(value, NULL, 0);
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("FirstBurstLength", text, end, &value, &value_end)) {
		    /* FIXME: no octal */
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			session->FirstBurstLength =
			    iscsi_strtoul(value, NULL, 0);
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_FIRSTBURSTLENGTH;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("MaxBurstLength", text, end, &value, &value_end)) {
		    /* we don't really care, since it's a
		     * limit on the target's R2Ts, but
		     * record it anwyay 
		     */
		    /* FIXME: no octal, and draft20 says we
		     * MUST NOT send more than
		     * MaxBurstLength 
		     */
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			session->MaxBurstLength = iscsi_strtoul(value, NULL, 0);
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_MAXBURSTLENGTH;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("HeaderDigest", text, end, &value, &value_end)) {
		    if (iscsi_strcmp(value, "None") == 0) {
			if (session->HeaderDigest != ISCSI_DIGEST_CRC32C) {
			    session->HeaderDigest = ISCSI_DIGEST_NONE;
			} else {
			    logmsg(AS_ERROR,
				   "login negotiation failed, HeaderDigest=CRC32C "
				   "is required, can't accept %s\n", text);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else if (iscsi_strcmp(value, "CRC32C") == 0) {
			if (session->HeaderDigest != ISCSI_DIGEST_NONE) {
			    session->HeaderDigest = ISCSI_DIGEST_CRC32C;
			} else {
			    logmsg(AS_ERROR,
				   "login negotiation failed, HeaderDigest=None "
				   "is required, can't accept %s\n", text);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else {
			logmsg(AS_ERROR,
			       "login negotiation failed, can't accept %s\n",
			       text);
			return LOGIN_NEGOTIATION_FAILED;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("DataDigest", text, end, &value, &value_end)) {
		    if (iscsi_strcmp(value, "None") == 0) {
			if (session->DataDigest != ISCSI_DIGEST_CRC32C) {
			    session->DataDigest = ISCSI_DIGEST_NONE;
			} else {
			    logmsg(AS_ERROR,
				   "login negotiation failed, DataDigest=CRC32C "
				   "is required, can't accept %s\n", text);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else if (iscsi_strcmp(value, "CRC32C") == 0) {
			if (session->DataDigest != ISCSI_DIGEST_NONE) {
			    session->DataDigest = ISCSI_DIGEST_CRC32C;
			} else {
			    logmsg(AS_ERROR,
				   "login negotiation failed, DataDigest=None is "
				   "required, can't accept %s\n", text);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else {
			logmsg(AS_ERROR,
			       "login negotiation failed, can't accept %s\n",
			       text);
			return LOGIN_NEGOTIATION_FAILED;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("DefaultTime2Wait", text, end, &value, &value_end)) {
		    session->DefaultTime2Wait = iscsi_strtoul(value, NULL, 0);
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("DefaultTime2Retain", text, end, &value, &value_end)) {
		    session->DefaultTime2Retain = iscsi_strtoul(value, NULL, 0);
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("OFMarker", text, end, &value, &value_end)) {
		    /* result function is AND, target must honor our No */
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("OFMarkInt", text, end, &value, &value_end)) {
		    /* we don't do markers, so we don't care */
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("IFMarker", text, end, &value, &value_end)) {
		    /* result function is AND, target must honor our No */
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("IFMarkInt", text, end, &value, &value_end)) {
		    /* we don't do markers, so we don't care */
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("DataPDUInOrder", text, end, &value, &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if (value && iscsi_strcmp(value, "Yes") == 0)
			    session->DataPDUInOrder = 1;
			else
			    session->DataPDUInOrder = 0;
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_DATAPDUINORDER;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("DataSequenceInOrder", text, end, &value,
			 &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if (value && iscsi_strcmp(value, "Yes") == 0)
			    session->DataSequenceInOrder = 1;
			else
			    session->DataSequenceInOrder = 0;
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_DATASEQUENCEINORDER;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("MaxOutstandingR2T", text, end, &value, &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if ((iscsi_strcmp(value, "1"))
			    && (session->type == ISCSI_SESSION_TYPE_NORMAL)) {
			    logmsg(AS_ERROR,
				   "login negotiation failed, can't accept "
				   "MaxOutstandingR2T %s\n", value);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_MAXOUTSTANDINGR2T;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("MaxConnections", text, end, &value, &value_end)) {
		    if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
			if ((iscsi_strcmp(value, "1"))
			    && (session->type == ISCSI_SESSION_TYPE_NORMAL)) {
			    logmsg(AS_ERROR,
				   "login negotiation failed, can't accept "
				   "MaxConnections %s\n", value);
			    return LOGIN_NEGOTIATION_FAILED;
			}
		    } else {
			session->irrelevant_keys_bitmap |=
			    IRRELEVANT_MAXCONNECTIONS;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("ErrorRecoveryLevel", text, end, &value, &value_end)) {
		    if (iscsi_strcmp(value, "0")) {
			logmsg(AS_ERROR,
			       "login negotiation failed, can't accept "
			       "ErrorRecovery %s\n", value);
			return LOGIN_NEGOTIATION_FAILED;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("X-com.cisco.protocol", text, end, &value,
			 &value_end)) {
		    if (iscsi_strcmp(value, "NotUnderstood")
			&& iscsi_strcmp(value, "Reject")
			&& iscsi_strcmp(value, "Irrelevant")
			&& iscsi_strcmp(value, "draft20")) {
			/* if we didn't get a compatible protocol, fail */
			logmsg(AS_ERROR,
			       "login version mismatch, can't accept protocol %s\n",
			       value);
			return LOGIN_VERSION_MISMATCH;
		    }
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("X-com.cisco.PingTimeout", text, end, &value,
			 &value_end)) {
		    /* we don't really care what the target ends up using */
		    text = value_end;
		} else
		    if (iscsi_find_key_value
			("X-com.cisco.sendAsyncText", text, end, &value,
			 &value_end)) {
		    /* we don't bother for the target response */
		    text = value_end;
		} else {
		    /* FIXME: we may want to ignore X- keys sent by
		     * the target, but that would require us to have
		     * another PDU buffer so that we can generate a
		     * response while we still know what keys we
		     * received, so that we can reply with a
		     * NotUnderstood response.  For now, reject logins
		     * with keys we don't understand.  Another option is
		     * to silently ignore them, and see if the target has
		     * a problem with that.  The danger there is we may
		     * get caught in an infinite loop where we send an empty
		     * PDU requesting a stage transition, and the target
		     * keeps sending an empty PDU denying a stage transition
		     * (because we haven't replied to it's key yet).
		     */
		    logmsg(AS_ERROR,
			   "login negotiation failed, couldn't recognize text %s\n",
			   text);
		    return LOGIN_NEGOTIATION_FAILED;
		}
		break;
	    }
	default:
	    return LOGIN_FAILED;
	}
    }

    if (session->current_stage == ISCSI_SECURITY_NEGOTIATION_STAGE) {
	switch (iscsiAuthClientRecvEnd
		(auth_client, null_callback, (void *) session, NULL)) {
	case iscsiAuthStatusContinue:
	    /* continue sending PDUs */
	    break;

	case iscsiAuthStatusPass:
	    logmsg(AS_DEBUG, "authenticated by target %s\n",
		   session->TargetName);
	    break;

	case iscsiAuthStatusInProgress:
	    /* this should only occur if we were authenticating the target,
	     * which we don't do yet, so treat this as an error.
	     */
	case iscsiAuthStatusNoError:	/* treat this as an error, since we
					 * should get a different code 
					 */
	case iscsiAuthStatusError:
	case iscsiAuthStatusFail:
	default:{
		int debug_status = 0;

		if (iscsiAuthClientGetDebugStatus(auth_client, &debug_status) !=
		    iscsiAuthStatusNoError) {
		    logmsg(AS_ERROR,
			   "login authentication failed with target %s, %s\n",
			   session->TargetName,
			   iscsiAuthClientDebugStatusToText(debug_status));
		} else {
		    logmsg(AS_ERROR,
			   "login authentication failed with target %s\n",
			   session->TargetName);
		}
		return LOGIN_AUTHENTICATION_FAILED;
	    }
	}
    }

    /* record some of the PDU fields for later use */
    session->tsih = iscsi_ntohs(login_rsp_pdu->tsih);
    session->ExpCmdSn = iscsi_ntohl(login_rsp_pdu->expcmdsn);
    session->MaxCmdSn = iscsi_ntohl(login_rsp_pdu->maxcmdsn);
    if (login_rsp_pdu->status_class == STATUS_CLASS_SUCCESS)
	session->ExpStatSn = iscsi_ntohl(login_rsp_pdu->statsn) + 1;

    if (transit) {
	/* advance to the next stage */
	session->partial_response = 0;
	session->current_stage =
	    login_rsp_pdu->flags & ISCSI_FLAG_LOGIN_NEXT_STAGE_MASK;
	session->irrelevant_keys_bitmap = 0;
    } else {
	/* we got a partial response, don't advance, more negotiation to do */
	session->partial_response = 1;
    }

    return LOGIN_OK;		/* this PDU is ok, though
				 * the login process may not
				 * be done yet */
}

int
iscsi_make_text_pdu(iscsi_session_t * session, struct IscsiHdr *pdu, char *data,
		    int max_data_length)
{
    struct IscsiTextHdr *text_pdu = (struct IscsiTextHdr *) pdu;

    /* initialize the PDU header */
    memset(text_pdu, 0, sizeof (*text_pdu));

    text_pdu->opcode = ISCSI_OP_TEXT_CMD;
    text_pdu->itt = iscsi_htonl(session->itt);
    text_pdu->ttt = RSVD_TASK_TAG;
    text_pdu->cmdsn = iscsi_htonl(session->CmdSn++);
    text_pdu->expstatsn = iscsi_htonl(session->ExpStatSn);

    return 1;
}

int
iscsi_make_login_pdu(iscsi_session_t * session, struct IscsiHdr *pdu,
		     char *data, int max_data_length)
{
    int transit = 0;
    char value[iscsiAuthStringMaxLength];
    struct IscsiLoginHdr *login_pdu = (struct IscsiLoginHdr *) pdu;
    IscsiAuthClient *auth_client = (session->auth_buffers
				    && session->num_auth_buffers) ?
	(IscsiAuthClient *) session->auth_buffers[0].address : NULL;

    /* initialize the PDU header */
    memset(login_pdu, 0, sizeof (*login_pdu));
    login_pdu->opcode = ISCSI_OP_LOGIN_CMD | ISCSI_OP_IMMEDIATE;
    login_pdu->cid = 0;
    memcpy(login_pdu->isid, session->isid, sizeof (session->isid));
    login_pdu->tsih = 0;
    login_pdu->cmdsn = iscsi_htonl(session->CmdSn);	/* don't increment
							 * on immediate 
							 */

    login_pdu->min_version = ISCSI_DRAFT20_VERSION;
    login_pdu->max_version = ISCSI_DRAFT20_VERSION;

    /* we have to send 0 until full-feature stage */
    login_pdu->expstatsn = iscsi_htonl(session->ExpStatSn);

    /* the very first Login PDU has some additional requirements, 
     * and we need to decide what stage to start in.
     */
    if (session->current_stage == ISCSI_INITIAL_LOGIN_STAGE) {
	if (session->InitiatorName && session->InitiatorName[0]) {
	    if (!iscsi_add_text
		(session, pdu, data, max_data_length, "InitiatorName",
		 session->InitiatorName))
		return 0;
	} else {
	    logmsg(AS_ERROR,
		   "InitiatorName is required on the first Login PDU\n");
	    return 0;
	}
	if (session->InitiatorAlias && session->InitiatorAlias[0]) {
	    if (!iscsi_add_text
		(session, pdu, data, max_data_length, "InitiatorAlias",
		 session->InitiatorAlias))
		return 0;
	}

	if ((session->TargetName[0] != '\0')
	    && (session->type == ISCSI_SESSION_TYPE_NORMAL)) {
	    if (!iscsi_add_text
		(session, pdu, data, max_data_length, "TargetName",
		 session->TargetName))
		return 0;
	}

	if (!iscsi_add_text(session, pdu, data, max_data_length, "SessionType",
			    (session->type ==
			     ISCSI_SESSION_TYPE_DISCOVERY) ? "Discovery" :
			    "Normal"))
	    return 0;

	if (auth_client) {
	    /* we're prepared to do authentication */
	    session->current_stage = session->next_stage =
		ISCSI_SECURITY_NEGOTIATION_STAGE;
	} else {
	    /* can't do any authentication, skip that stage */
	    session->current_stage = session->next_stage =
		ISCSI_OP_PARMS_NEGOTIATION_STAGE;
	}
    }

    /* fill in text based on the stage */
    switch (session->current_stage) {
    case ISCSI_OP_PARMS_NEGOTIATION_STAGE:{
	    /* we always try to go from op params to full feature stage */
	    session->current_stage = ISCSI_OP_PARMS_NEGOTIATION_STAGE;
	    session->next_stage = ISCSI_FULL_FEATURE_PHASE;
	    transit = 1;

	    /* the terminology here may have gotten dated.  a partial
	     * response is a login response that doesn't complete a
	     * login.  If we haven't gotten a partial response, then
	     * either we shouldn't be here, or we just switched to
	     * this stage, and need to start offering keys.  
	     */
	    if (!session->partial_response) {
		/* request the desired settings the first
		 * time we are in this stage 
		 */
		switch (session->HeaderDigest) {
		case ISCSI_DIGEST_NONE:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "HeaderDigest",
			 "None"))
			return 0;
		    break;
		case ISCSI_DIGEST_CRC32C:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "HeaderDigest",
			 "CRC32C"))
			return 0;
		    break;
		case ISCSI_DIGEST_CRC32C_NONE:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "HeaderDigest",
			 "CRC32C,None"))
			return 0;
		    break;
		default:
		case ISCSI_DIGEST_NONE_CRC32C:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "HeaderDigest",
			 "None,CRC32C"))
			return 0;
		    break;
		}

		switch (session->DataDigest) {
		case ISCSI_DIGEST_NONE:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataDigest",
			 "None"))
			return 0;
		    break;
		case ISCSI_DIGEST_CRC32C:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataDigest",
			 "CRC32C"))
			return 0;
		    break;
		case ISCSI_DIGEST_CRC32C_NONE:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataDigest",
			 "CRC32C,None"))
			return 0;
		    break;
		default:
		case ISCSI_DIGEST_NONE_CRC32C:
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataDigest",
			 "None,CRC32C"))
			return 0;
		    break;
		}

		iscsi_sprintf(value, "%d", session->MaxRecvDataSegmentLength);
		if (!iscsi_add_text
		    (session, pdu, data, max_data_length,
		     "MaxRecvDataSegmentLength", value))
		    return 0;

		iscsi_sprintf(value, "%d", session->DefaultTime2Wait);
		if (!iscsi_add_text
		    (session, pdu, data, max_data_length, "DefaultTime2Wait",
		     value))
		    return 0;

		iscsi_sprintf(value, "%d", session->DefaultTime2Retain);
		if (!iscsi_add_text
		    (session, pdu, data, max_data_length, "DefaultTime2Retain",
		     value))
		    return 0;

		if (!iscsi_add_text
		    (session, pdu, data, max_data_length, "ErrorRecoveryLevel",
		     "0"))
		    return 0;

		if (!iscsi_add_text
		    (session, pdu, data, max_data_length, "IFMarker", "No"))
		    return 0;

		if (!iscsi_add_text
		    (session, pdu, data, max_data_length, "OFMarker", "No"))
		    return 0;

		if (session->type == ISCSI_SESSION_TYPE_NORMAL) {
		    /* these are only relevant for normal sessions */
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "InitialR2T",
			 session->InitialR2T ? "Yes" : "No"))
			return 0;

		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "ImmediateData",
			 session->ImmediateData ? "Yes" : "No"))
			return 0;

		    iscsi_sprintf(value, "%d", session->MaxBurstLength);
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "MaxBurstLength",
			 value))
			return 0;

		    iscsi_sprintf(value, "%d", session->FirstBurstLength);
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "FirstBurstLength", value))
			return 0;

		    /* these we must have */
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "MaxOutstandingR2T", "1"))
			return 0;
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "MaxConnections",
			 "1"))
			return 0;

		    /* FIXME: the caller may want different settings for these. */
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataPDUInOrder",
			 "Yes"))
			return 0;
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "DataSequenceInOrder", "Yes"))
			return 0;
		}

		/* Note: 12.22 forbids vendor-specific keys
		 * on discovery sessions, so the caller is
		 * violating the spec if it asks for these
		 * on a discovery session.
		 */
		if (session->vendor_specific_keys) {
		    /* adjust the target's PingTimeout for
		     * normal sessions, so that it matches
		     * the driver's ping timeout.  The
		     * network probably has the same latency
		     * in both directions, so the values
		     * ought to match.
		     */
		    if (session->ping_timeout >= 0) {
			iscsi_sprintf(value, "%d", session->ping_timeout);
			if (!iscsi_add_text
			    (session, pdu, data, max_data_length,
			     "X-com.cisco.PingTimeout", value))
			    return 0;
		    }

		    if (session->send_async_text >= 0) {
			if (!iscsi_add_text
			    (session, pdu, data, max_data_length,
			     "X-com.cisco.sendAsyncText",
			     session->send_async_text ? "Yes" : "No"))
			    return 0;
		    }
		    /* vendor-specific protocol
		     * specification. list of protocol level
		     * strings in order of preference
		     * allowable values are: draft<n>
		     * (e.g. draft8), rfc<n> (e.g. rfc666).
		     * For example:
		     * "X-com.cisco.protocol=draft20,draft8"
		     * requests draft 20, or 8 if 20 isn't
		     * supported.
		     * "X-com.cisco.protocol=draft8,draft20"
		     * requests draft 8, or 20 if 8 isn't
		     * supported.
		     *
		     * Targets that understand this key
		     * SHOULD return the protocol level they
		     * selected as a response to this key,
		     * though the active_version may be
		     * sufficient to distinguish which
		     * protocol was chosen.
		     *
		     * Note: This probably won't work unless
		     * we start in op param stage, since the
		     * security stage limits what keys we
		     * can send, and we'd need to have sent
		     * this on the first PDU of the login.
		     * Keep sending it for informational
		     * use, and so that we can sanity check
		     * things later if the RFC and draft20
		     * are using the same active version
		     * number, but have non-trivial
		     * differences.
		     */
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "X-com.cisco.protocol", "draft20"))
			return 0;
		}
	    } else {

		/* If you receive irrelevant keys, just check
		 * them from the irrelevant keys bitmap and
		 * respond with the key=Irrelevant text
		 */

		if (session->irrelevant_keys_bitmap & IRRELEVANT_MAXCONNECTIONS) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "MaxConnections",
			 "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap & IRRELEVANT_INITIALR2T) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "InitialR2T",
			 "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap & IRRELEVANT_IMMEDIATEDATA) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "ImmediateData",
			 "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap & IRRELEVANT_MAXBURSTLENGTH) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "MaxBurstLength",
			 "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap &
		    IRRELEVANT_FIRSTBURSTLENGTH) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "FirstBurstLength", "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap &
		    IRRELEVANT_MAXOUTSTANDINGR2T) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "MaxOutstandingR2T", "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap & IRRELEVANT_DATAPDUINORDER) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length, "DataPDUInOrder",
			 "Irrelevant"))
			return 0;
		}

		if (session->irrelevant_keys_bitmap &
		    IRRELEVANT_DATASEQUENCEINORDER) {
		    if (!iscsi_add_text
			(session, pdu, data, max_data_length,
			 "DataSequenceInOrder", "Irrelevant"))
			return 0;
		}
	    }
	    break;
	}
    case ISCSI_SECURITY_NEGOTIATION_STAGE:{
	    int keytype = iscsiAuthKeyTypeNone;
	    int rc = iscsiAuthClientSendTransitBit(auth_client, &transit);

	    /* see if we're ready for a stage change */
	    if (rc == iscsiAuthStatusNoError) {
		if (transit) {
		    /* discovery sessions can go right to
		     * full-feature phase, unless they want
		     * to non-standard values for the few
		     * relevant keys, or want to offer
		     * vendor-specific keys.
		     */
		    if (session->type == ISCSI_SESSION_TYPE_DISCOVERY) {
			if ((session->HeaderDigest != ISCSI_DIGEST_NONE) ||
			    (session->DataDigest != ISCSI_DIGEST_NONE) ||
			    (session->MaxRecvDataSegmentLength !=
			     DEFAULT_MAX_RECV_DATA_SEGMENT_LENGTH)
			    || (session->vendor_specific_keys)) {
			    session->next_stage =
				ISCSI_OP_PARMS_NEGOTIATION_STAGE;
			} else {
			    session->next_stage = ISCSI_FULL_FEATURE_PHASE;
			}
		    } else {
			session->next_stage = ISCSI_OP_PARMS_NEGOTIATION_STAGE;
		    }
		} else {
		    session->next_stage = ISCSI_SECURITY_NEGOTIATION_STAGE;
		}
	    } else {
		return 0;
	    }

	    /* enumerate all the keys the auth code might want to send */
	    while (iscsiAuthClientGetNextKeyType(&keytype) ==
		   iscsiAuthStatusNoError) {
		int present = 0;
		char *key = (char *) iscsiAuthClientGetKeyName(keytype);
		int key_length = key ? iscsi_strlen(key) : 0;
		int pdu_length = ntoh24(pdu->dlength);
		char *auth_value = data + pdu_length + key_length + 1;
		unsigned int max_length = max_data_length - (pdu_length + key_length + 1);	/* FIXME: check this */

		/* add the key/value pairs the auth code
		 * wants to send directly to the PDU, since
		 * they could in theory be large.
		 */
		rc = iscsiAuthClientSendKeyValue(auth_client, keytype, &present,
						 auth_value, max_length);
		if ((rc == iscsiAuthStatusNoError) && present) {
		    /* actually fill in the key */
		    strncpy(&data[pdu_length], key, key_length);
		    pdu_length += key_length;
		    data[pdu_length] = '=';
		    pdu_length++;
		    /* adjust the PDU's data segment length
		     * to include the value and trailing
		     * NULL 
		     */
		    pdu_length += iscsi_strlen(auth_value) + 1;
		    hton24(pdu->dlength, pdu_length);
		}
	    }

	    break;
	}
    case ISCSI_FULL_FEATURE_PHASE:
	logmsg(AS_ERROR, "can't send login PDUs in full feature phase\n");
	return 0;
    default:
	logmsg(AS_ERROR, "can't send login PDUs in unknown stage %d\n",
	       session->current_stage);
	return 0;
    }

    /* fill in the flags */
    login_pdu->flags = 0;
    login_pdu->flags |= session->current_stage << 2;
    if (transit) {
	/* transit to the next stage */
	login_pdu->flags |= session->next_stage;
	login_pdu->flags |= ISCSI_FLAG_LOGIN_TRANSIT;
    } else {
	/* next == current */
	login_pdu->flags |= session->current_stage;
    }
    return 1;
}

/* attempt to login to the target.
 * The caller must check the status class to determine if the login succeeded.
 * A return of 1 does not mean the login succeeded, it just means this function
 * worked, and the status class is valid info.  This allows the caller to decide
 * whether or not to retry logins, so that we don't have any policy logic here.
 */
iscsi_login_status_t
iscsi_login(iscsi_session_t * session, char *buffer, size_t bufsize,
	    uint8_t * status_class, uint8_t * status_detail)
{
    IscsiAuthClient *auth_client = NULL;
    int received_pdu = 0;
    iscsi_login_status_t ret = LOGIN_FAILED;

    /* prepare the session */
    session->CmdSn = 1;
    session->ExpCmdSn = 1;
    session->MaxCmdSn = 1;
    session->ExpStatSn = 0;

    session->current_stage = ISCSI_INITIAL_LOGIN_STAGE;
    session->partial_response = 0;

    if (session->auth_buffers && session->num_auth_buffers) {
	auth_client = (IscsiAuthClient *) session->auth_buffers[0].address;

	/* prepare for authentication */
	if (iscsiAuthClientInit
	    (iscsiAuthNodeTypeInitiator, session->num_auth_buffers,
	     session->auth_buffers) != iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR, "couldn't initialize authentication\n");
	    return LOGIN_FAILED;
	}

	if (iscsiAuthClientSetVersion(auth_client, iscsiAuthVersionRfc) !=
	    iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR, "couldn't set authentication version RFC\n");
	    goto done;
	}

	if (session->username
	    && (iscsiAuthClientSetUsername(auth_client, session->username) !=
		iscsiAuthStatusNoError)) {
	    logmsg(AS_ERROR, "couldn't set username\n");
	    goto done;
	}

	if (session->password
	    &&
	    (iscsiAuthClientSetPassword
	     (auth_client, session->password,
	      session->password_length) != iscsiAuthStatusNoError)) {
	    logmsg(AS_ERROR, "couldn't set password\n");
	    goto done;
	}

	/* FIXME: we disable the minimum size check for now */
	if (iscsiAuthClientSetIpSec(auth_client, 1) != iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR, "couldn't set IPSec\n");
	    goto done;
	}

	if (iscsiAuthClientSetAuthRemote
	    (auth_client,
	     session->bidirectional_auth) != iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR, "couldn't set remote authentication\n");
	    goto done;
	}
    }

    /* exchange PDUs until the login stage is complete, or an error occurs */
    do {
	struct IscsiHdr pdu;
	struct IscsiLoginRspHdr *login_rsp_pdu =
	    (struct IscsiLoginRspHdr *) &pdu;
	char *data;
	int max_data_length;
	int timeout = 0;

	memset(buffer, 0, bufsize);

	data = buffer;
	max_data_length = bufsize;

	ret = LOGIN_FAILED;

	/* pick the appropriate timeout. If we know the target has
	 * responded before, and we're in the security stage, we use a
	 * longer timeout, since the authentication alogorithms can
	 * take a while, especially if the target has to go talk to a
	 * tacacs or RADIUS server (which may or may not be
	 * responding).
	 */
	if (received_pdu
	    && (session->current_stage == ISCSI_SECURITY_NEGOTIATION_STAGE))
	    timeout = session->auth_timeout;
	else
	    timeout = session->login_timeout;

	/* fill in the PDU header and text data based on the
	 * login stage that we're in 
	 */
	if (!iscsi_make_login_pdu(session, &pdu, data, max_data_length)) {
	    logmsg(AS_ERROR, "login failed, couldn't make a login PDU\n");
	    ret = LOGIN_FAILED;
	    goto done;
	}

	/* send a PDU to the target */
	if (!iscsi_send_pdu(session, &pdu, data, timeout)) {
	    /* FIXME: caller might want us to distinguish I/O error and timeout.
	     * might want to switch portals on timeouts, but not I/O errors.
	     */
	    logmsg(AS_ERROR, "login I/O error, failed to send a PDU\n");
	    ret = LOGIN_IO_ERROR;
	    goto done;
	}

	/* read the target's response into the same buffer */
	memset(buffer, 0, bufsize);
	if (!iscsi_recv_pdu
	    (session, &pdu, sizeof (pdu), data, max_data_length, timeout)) {
	    /* FIXME: caller might want us to distinguish I/O error and timeout.
	     * might want to switch portals on timeouts, but not I/O errors.
	     */
	    logmsg(AS_ERROR, "login I/O error, failed to receive a PDU\n");
	    ret = LOGIN_IO_ERROR;
	    goto done;
	}

	received_pdu = 1;

	/* check the PDU response type */
	if (pdu.opcode == (ISCSI_OP_LOGIN_RSP | 0xC0)) {
	    /* it's probably a draft 8 login response, which
	     * we can't deal with 
	     */
	    logmsg(AS_ERROR,
		   "received iSCSI draft 8 login response opcode 0x%x, "
		   "expected draft 20 login response 0x%2x\n",
		   pdu.opcode, ISCSI_OP_LOGIN_RSP);
	    logmsg(AS_ERROR,
		   "please make sure that you have installed the correct "
		   "driver version.\n");
	    ret = LOGIN_VERSION_MISMATCH;
	    goto done;
	} else if (pdu.opcode != ISCSI_OP_LOGIN_RSP) {
	    logmsg(AS_ERROR,
		   "received invalud PDU during login, opcode 0x%2x, "
		   "expected login response opcode 0x%2x\n",
		   pdu.opcode, ISCSI_OP_LOGIN_RSP);
	    ret = LOGIN_INVALID_PDU;
	    goto done;
	}

	/* give the caller the status class and detail from
	 * the last login response PDU received 
	 */
	if (status_class)
	    *status_class = login_rsp_pdu->status_class;
	if (status_detail)
	    *status_detail = login_rsp_pdu->status_detail;

	switch (login_rsp_pdu->status_class) {
	case STATUS_CLASS_SUCCESS:
	    /* process this response and possibly continue sending PDUs */
	    ret =
		iscsi_process_login_response(session, login_rsp_pdu, data,
					     max_data_length);
	    if (ret != LOGIN_OK)	/* pass back whatever error we 
					 * discovered 
					 */
		goto done;
	    break;
	case STATUS_CLASS_REDIRECT:
	    /* we need to process this response to get the
	     * TargetAddress of the redirect, but we don't
	     * care about the return code.  FIXME: we really
	     * only need to process a TargetAddress, but
	     * there shouldn't be any other keys.
	     */
	    iscsi_process_login_response(session, login_rsp_pdu, data,
					 max_data_length);
	    ret = LOGIN_OK;
	    goto done;
	case STATUS_CLASS_INITIATOR_ERR:
	    if (login_rsp_pdu->status_detail == ISCSI_LOGIN_STATUS_AUTH_FAILED) {
		logmsg(AS_ERROR,
		       "login failed to authenticate with target %s\n",
		       session->TargetName);
	    }
	    ret = LOGIN_OK;
	    goto done;
	default:
	    /* some sort of error, login terminated
	     * unsuccessfully, though this function did it's
	     * job.  the caller must check the status_class
	     * and status_detail and decide what to do next.
	     */
	    ret = LOGIN_OK;
	    goto done;
	}

    } while (session->current_stage != ISCSI_FULL_FEATURE_PHASE);

    ret = LOGIN_OK;

  done:
    if (auth_client) {
	if (iscsiAuthClientFinish(auth_client) != iscsiAuthStatusNoError) {
	    logmsg(AS_ERROR, "login failed, error finishing authClient\n");
	    if (ret == LOGIN_OK)
		ret = LOGIN_FAILED;
	}
	/* FIXME: clear the temp buffers as well? */
    }

    return ret;
}


/*
 * Make a best effort to logout the session, then disconnect the
 * socket.
 */
void
iscsi_logout_and_disconnect(iscsi_session_t * session)
{
    struct IscsiLogoutHdr logout_req;
    struct IscsiLogoutRspHdr logout_resp;
    int rc;

    /*
     * Build logout request header
     */
    memset(&logout_req, 0, sizeof (logout_req));
    logout_req.opcode = ISCSI_OP_LOGOUT_CMD | ISCSI_OP_IMMEDIATE;
    logout_req.flags = ISCSI_FLAG_FINAL |
	(ISCSI_LOGOUT_REASON_CLOSE_SESSION & ISCSI_FLAG_LOGOUT_REASON_MASK);
    logout_req.itt = htonl(session->itt);
    if (++session->itt == RSVD_TASK_TAG)
	session->itt = 1;
    logout_req.cmdsn = htonl(session->CmdSn);
    logout_req.expstatsn = htonl(++session->ExpStatSn);

    /*
     * Send the logout request
     */
    rc = iscsi_send_pdu(session,
			(struct IscsiHdr *)&logout_req, /* logout header */
			NULL, /* no data */
			3 /* timeout, in seconds */);
    if (!rc) {
	logmsg(AS_ERROR, "iscsid: iscsi_logout - failed to send logout PDU.\n");
	goto done;
    }

    /*
     * Read the login response
     */
    memset(&logout_resp, 0, sizeof(logout_resp));
    rc = iscsi_recv_pdu (session,
			 (struct IscsiHdr *)&logout_resp,
			 sizeof (logout_resp),
			 NULL,
			 0,
			 1 /* timeout, in seconds */);
    if (!rc) {
	logmsg(AS_ERROR, "iscsid: logout - failed to receive logout resp\n");
	goto done;
    }
    if (logout_resp.response != ISCSI_LOGOUT_SUCCESS) {
	logmsg(AS_ERROR, "iscsid: logout failed - response = 0x%x\n",
	       logout_resp.response);
    }
    
 done:    
    /*
     * Close the socket.
     */
    iscsi_disconnect(session);
}

