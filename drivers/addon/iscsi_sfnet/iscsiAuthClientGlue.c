/*
 * iSCSI connection daemon
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
 * $Id: iscsiAuthClientGlue.c,v 1.6.2.1 2004/08/10 23:04:45 coughlan Exp $ 
 *
 */

#include "iscsiAuthClient.h"
#include "iscsi-platform.h"
#include "iscsi-protocol.h"
#include "iscsi-session.h"

/*
 * Authenticate a target's CHAP response.
 */
int
iscsiAuthClientChapAuthRequest(IscsiAuthClient * client,
			       char *username, unsigned int id,
			       unsigned char *challengeData,
			       unsigned int challengeLength,
			       unsigned char *responseData,
			       unsigned int responseLength)
{
    iscsi_session_t *session = (iscsi_session_t *) client->userHandle;
    IscsiAuthMd5Context context;
    unsigned char verifyData[16];

    if (session == NULL) {
	return iscsiAuthStatusFail;
    }

    /* the expected credentials are in the session */
    if (session->username_in == NULL) {
	logmsg(AS_ERROR,
	       "failing authentication, no incoming username configured "
	       "to authenticate target %s\n", session->TargetName);
	return iscsiAuthStatusFail;
    }
    if (iscsi_strcmp(username, session->username_in) != 0) {
	logmsg(AS_ERROR,
	       "failing authentication, received incorrect username from "
	       "target %s\n", session->TargetName);
	return iscsiAuthStatusFail;
    }

    if ((session->password_length_in < 1) || (session->password_in == NULL)
	|| (session->password_in[0] == '\0')) {
	logmsg(AS_ERROR,
	       "failing authentication, no incoming password configured "
	       "to authenticate target %s\n", session->TargetName);
	return iscsiAuthStatusFail;
    }

    /* challenge length is I->T, and shouldn't need to be checked */

    if (responseLength != sizeof (verifyData)) {
	logmsg(AS_ERROR,
	       "failing authentication, received incorrect CHAP response "
	       "length %u from target %s\n",
	       responseLength, session->TargetName);
	return iscsiAuthStatusFail;
    }

    iscsiAuthMd5Init(&context);

    /* id byte */
    verifyData[0] = id;
    iscsiAuthMd5Update(&context, verifyData, 1);

    /* shared secret */
    iscsiAuthMd5Update(&context, (unsigned char *) session->password_in,
		       session->password_length_in);

    /* challenge value */
    iscsiAuthMd5Update(&context, (unsigned char *) challengeData,
		       challengeLength);

    iscsiAuthMd5Final(verifyData, &context);

    if (iscsi_memcmp(responseData, verifyData, sizeof (verifyData)) == 0) {
	debugmsg(1, "initiator authenticated target %s\n", session->TargetName);
	return iscsiAuthStatusPass;
    }

    logmsg(AS_ERROR,
	   "failing authentication, received incorrect CHAP response from "
	   "target %s\n", session->TargetName);
    return iscsiAuthStatusFail;
}

void
iscsiAuthClientChapAuthCancel(IscsiAuthClient * client)
{
}

int
iscsiAuthClientTextToNumber(const char *text, unsigned long *pNumber)
{
    char *pEnd;
    unsigned long number;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
	number = iscsi_strtoul(text + 2, &pEnd, 16);
    } else {
	number = iscsi_strtoul(text, &pEnd, 10);
    }

    if (*text != '\0' && *pEnd == '\0') {
	*pNumber = number;
	return 0;		/* No error */
    } else {
	return 1;		/* Error */
    }
}

void
iscsiAuthClientNumberToText(unsigned long number, char *text,
			    unsigned int length)
{
    iscsi_sprintf(text, "%lu", number);
}

void
iscsiAuthRandomSetData(unsigned char *data, unsigned int length)
{
#if defined(__KERNEL__)
    get_random_bytes(data, length);
#endif
}

void
iscsiAuthMd5Init(IscsiAuthMd5Context * context)
{
    MD5Init(context);
}

void
iscsiAuthMd5Update(IscsiAuthMd5Context * context, unsigned char *data,
		   unsigned int length)
{
    MD5Update(context, data, length);
}

void
iscsiAuthMd5Final(unsigned char *hash, IscsiAuthMd5Context * context)
{
    MD5Final(hash, context);
}

int
iscsiAuthClientData(unsigned char *outData, unsigned int *outLength,
		    unsigned char *inData, unsigned int inLength)
{
    if (*outLength < inLength)
	return 1;		/* error */

    memcpy(outData, inData, inLength);
    *outLength = inLength;

    return 0;			/* no error */
}
