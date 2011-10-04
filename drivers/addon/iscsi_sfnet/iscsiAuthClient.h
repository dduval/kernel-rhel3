#ifndef ISCSIAUTHCLIENT_H
#define ISCSIAUTHCLIENT_H

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
 * $Id: iscsiAuthClient.h,v 1.9.2.1 2004/08/10 23:04:45 coughlan Exp $
 */

/*
 * This file is the include file for for iscsiAuthClient.c
 */

#ifdef __cplusplus
extern "C" {
#endif

    enum { iscsiAuthStringMaxLength = 256 };
    enum { iscsiAuthStringBlockMaxLength = 1024 };
    enum { iscsiAuthLargeBinaryMaxLength = 1024 };

    enum { iscsiAuthRecvEndMaxCount = 10 };

    enum { iscsiAuthClientSignature = 0x5984B2E3 };

    enum { iscsiAuthChapResponseLength = 16 };

/*
 * Note: The ordering of these values are chosen to match
 *       the ordering of the keys as shown in the iSCSI spec.
 *       The table IscsiAuthClientKeyInfo in iscsiAuthClient.c
 *       must also match this order.
 */
    enum iscsiAuthKeyType_t {
	iscsiAuthKeyTypeNone = -1,
	iscsiAuthKeyTypeFirst = 0,
	iscsiAuthKeyTypeAuthMethod = iscsiAuthKeyTypeFirst,
	iscsiAuthKeyTypeChapAlgorithm,
	iscsiAuthKeyTypeChapUsername,
	iscsiAuthKeyTypeChapResponse,
	iscsiAuthKeyTypeChapIdentifier,
	iscsiAuthKeyTypeChapChallenge,
	iscsiAuthKeyTypeMaxCount,
	iscsiAuthKeyTypeLast = iscsiAuthKeyTypeMaxCount - 1
    };
    typedef enum iscsiAuthKeyType_t IscsiAuthKeyType;

    enum {
	/* Common options for all keys. */
	iscsiAuthOptionReject = -2,
	iscsiAuthOptionNotPresent = -1,
	iscsiAuthOptionNone = 1,

	iscsiAuthMethodChap = 2,
	iscsiAuthMethodMaxCount = 2,

	iscsiAuthChapAlgorithmMd5 = 5,
	iscsiAuthChapAlgorithmMaxCount = 2
    };

    enum iscsiAuthNegRole_t {
	iscsiAuthNegRoleOriginator = 1,
	iscsiAuthNegRoleResponder = 2
    };
    typedef enum iscsiAuthNegRole_t IscsiAuthNegRole;

/*
 * Note: These values are chosen to map to the values sent
 *       in the iSCSI header.
 */
    enum iscsiAuthVersion_t {
	iscsiAuthVersionDraft8 = 2,
	iscsiAuthVersionRfc = 0
    };
    typedef enum iscsiAuthVersion_t IscsiAuthVersion;

    enum iscsiAuthStatus_t {
	iscsiAuthStatusNoError = 0,
	iscsiAuthStatusError,
	iscsiAuthStatusPass,
	iscsiAuthStatusFail,
	iscsiAuthStatusContinue,
	iscsiAuthStatusInProgress
    };
    typedef enum iscsiAuthStatus_t IscsiAuthStatus;

    enum iscsiAuthDebugStatus_t {
	iscsiAuthDebugStatusNotSet = 0,

	iscsiAuthDebugStatusAuthPass,
	iscsiAuthDebugStatusAuthRemoteFalse,

	iscsiAuthDebugStatusAuthFail,

	iscsiAuthDebugStatusAuthMethodBad,
	iscsiAuthDebugStatusChapAlgorithmBad,
	iscsiAuthDebugStatusPasswordDecryptFailed,
	iscsiAuthDebugStatusPasswordTooShortWithNoIpSec,
	iscsiAuthDebugStatusAuthServerError,
	iscsiAuthDebugStatusAuthStatusBad,
	iscsiAuthDebugStatusAuthPassNotValid,
	iscsiAuthDebugStatusSendDuplicateSetKeyValue,
	iscsiAuthDebugStatusSendStringTooLong,
	iscsiAuthDebugStatusSendTooMuchData,

	iscsiAuthDebugStatusAuthMethodExpected,
	iscsiAuthDebugStatusChapAlgorithmExpected,
	iscsiAuthDebugStatusChapIdentifierExpected,
	iscsiAuthDebugStatusChapChallengeExpected,
	iscsiAuthDebugStatusChapResponseExpected,
	iscsiAuthDebugStatusChapUsernameExpected,

	iscsiAuthDebugStatusAuthMethodNotPresent,
	iscsiAuthDebugStatusAuthMethodReject,
	iscsiAuthDebugStatusAuthMethodNone,
	iscsiAuthDebugStatusChapAlgorithmReject,
	iscsiAuthDebugStatusChapChallengeReflected,
	iscsiAuthDebugStatusPasswordIdentical,

	iscsiAuthDebugStatusLocalPasswordNotSet,

	iscsiAuthDebugStatusChapIdentifierBad,
	iscsiAuthDebugStatusChapChallengeBad,
	iscsiAuthDebugStatusChapResponseBad,
	iscsiAuthDebugStatusUnexpectedKeyPresent,
	iscsiAuthDebugStatusTbitSetIllegal,
	iscsiAuthDebugStatusTbitSetPremature,

	iscsiAuthDebugStatusRecvMessageCountLimit,
	iscsiAuthDebugStatusRecvDuplicateSetKeyValue,
	iscsiAuthDebugStatusRecvStringTooLong,
	iscsiAuthDebugStatusRecvTooMuchData
    };
    typedef enum iscsiAuthDebugStatus_t IscsiAuthDebugStatus;

    enum iscsiAuthNodeType_t {
	iscsiAuthNodeTypeInitiator = 1,
	iscsiAuthNodeTypeTarget = 2
    };
    typedef enum iscsiAuthNodeType_t IscsiAuthNodeType;

    enum iscsiAuthPhase_t {
	iscsiAuthPhaseConfigure = 1,
	iscsiAuthPhaseNegotiate,
	iscsiAuthPhaseAuthenticate,
	iscsiAuthPhaseDone,
	iscsiAuthPhaseError
    };
    typedef enum iscsiAuthPhase_t IscsiAuthPhase;

    enum iscsiAuthLocalState_t {
	iscsiAuthLocalStateSendAlgorithm = 1,
	iscsiAuthLocalStateRecvAlgorithm,
	iscsiAuthLocalStateRecvChallenge,
	iscsiAuthLocalStateDone,
	iscsiAuthLocalStateError
    };
    typedef enum iscsiAuthLocalState_t IscsiAuthLocalState;

    enum iscsiAuthRemoteState_t {
	iscsiAuthRemoteStateSendAlgorithm = 1,
	iscsiAuthRemoteStateSendChallenge,
	iscsiAuthRemoteStateRecvResponse,
	iscsiAuthRemoteStateAuthRequest,
	iscsiAuthRemoteStateDone,
	iscsiAuthRemoteStateError
    };
    typedef enum iscsiAuthRemoteState_t IscsiAuthRemoteState;

    typedef void IscsiAuthClientCallback(void *, void *, int);

    struct iscsiAuthClientGlobalStats_t {
	unsigned long requestSent;
	unsigned long responseReceived;
    };
    typedef struct iscsiAuthClientGlobalStats_t IscsiAuthClientGlobalStats;

    struct iscsiAuthBufferDesc_t {
	unsigned int length;
	void *address;
    };
    typedef struct iscsiAuthBufferDesc_t IscsiAuthBufferDesc;

    struct iscsiAuthKey_t {
	unsigned int present:1;
	unsigned int processed:1;
	unsigned int valueSet:1;
	char *string;
    };
    typedef struct iscsiAuthKey_t IscsiAuthKey;

    struct iscsiAuthLargeBinaryKey_t {
	unsigned int length;
	unsigned char *largeBinary;
    };
    typedef struct iscsiAuthLargeBinaryKey_t IscsiAuthLargeBinaryKey;

    struct iscsiAuthKeyBlock_t {
	unsigned int transitBit:1;
	unsigned int duplicateSet:1;
	unsigned int stringTooLong:1;
	unsigned int tooMuchData:1;
	unsigned int blockLength:16;
	char *stringBlock;
	IscsiAuthKey key[iscsiAuthKeyTypeMaxCount];
    };
    typedef struct iscsiAuthKeyBlock_t IscsiAuthKeyBlock;

    struct iscsiAuthStringBlock_t {
	char stringBlock[iscsiAuthStringBlockMaxLength];
    };
    typedef struct iscsiAuthStringBlock_t IscsiAuthStringBlock;

    struct iscsiAuthLargeBinary_t {
	unsigned char largeBinary[iscsiAuthLargeBinaryMaxLength];
    };
    typedef struct iscsiAuthLargeBinary_t IscsiAuthLargeBinary;

    struct iscsiAuthClient_t {
	unsigned long signature;

	void *glueHandle;
	struct iscsiAuthClient_t *next;
	unsigned int authRequestId;

	IscsiAuthNodeType nodeType;
	unsigned int authMethodCount;
	int authMethodList[iscsiAuthMethodMaxCount];
	IscsiAuthNegRole authMethodNegRole;
	unsigned int chapAlgorithmCount;
	int chapAlgorithmList[iscsiAuthChapAlgorithmMaxCount];
	int authRemote;
	char username[iscsiAuthStringMaxLength];
	int passwordPresent;
	unsigned int passwordLength;
	unsigned char passwordData[iscsiAuthStringMaxLength];
	char methodListName[iscsiAuthStringMaxLength];
	IscsiAuthVersion version;
	unsigned int chapChallengeLength;
	int ipSec;
	int base64;

	unsigned int authMethodValidCount;
	int authMethodValidList[iscsiAuthMethodMaxCount];
	int authMethodValidNegRole;
	const char *rejectOptionName;
	const char *noneOptionName;

	int recvInProgressFlag;
	int recvEndCount;
	IscsiAuthClientCallback *callback;
	void *userHandle;
	void *messageHandle;

	IscsiAuthPhase phase;
	IscsiAuthLocalState localState;
	IscsiAuthRemoteState remoteState;
	IscsiAuthStatus remoteAuthStatus;
	IscsiAuthDebugStatus debugStatus;
	int negotiatedAuthMethod;
	int negotiatedChapAlgorithm;
	int authResponseFlag;
	int authServerErrorFlag;
	int transitBitSentFlag;

	unsigned int sendChapIdentifier;
	IscsiAuthLargeBinaryKey sendChapChallenge;
	char chapUsername[iscsiAuthStringMaxLength];

	int recvChapChallengeStatus;
	IscsiAuthLargeBinaryKey recvChapChallenge;

	char scratchKeyValue[iscsiAuthStringMaxLength];

	IscsiAuthKeyBlock recvKeyBlock;
	IscsiAuthKeyBlock sendKeyBlock;
    };
    typedef struct iscsiAuthClient_t IscsiAuthClient;

#ifdef __cplusplus
}
#endif
#include "iscsiAuthClientGlue.h"
#ifdef __cplusplus
extern "C" {
#endif

    extern IscsiAuthClientGlobalStats iscsiAuthClientGlobalStats;

    extern int iscsiAuthClientInit(int, int, IscsiAuthBufferDesc *);
    extern int iscsiAuthClientFinish(IscsiAuthClient *);

    extern int iscsiAuthClientRecvBegin(IscsiAuthClient *);
    extern int iscsiAuthClientRecvEnd(IscsiAuthClient *,
				      IscsiAuthClientCallback *, void *,
				      void *);

    extern const char *iscsiAuthClientGetKeyName(int);
    extern int iscsiAuthClientGetNextKeyType(int *);
    extern int iscsiAuthClientKeyNameToKeyType(const char *);
    extern int iscsiAuthClientRecvKeyValue(IscsiAuthClient *, int,
					   const char *);
    extern int iscsiAuthClientSendKeyValue(IscsiAuthClient *, int, int *,
					   char *, unsigned int);
    extern int iscsiAuthClientRecvTransitBit(IscsiAuthClient *, int);
    extern int iscsiAuthClientSendTransitBit(IscsiAuthClient *, int *);

    extern int iscsiAuthClientSetAuthMethodList(IscsiAuthClient *, unsigned int,
						const int *);
    extern int iscsiAuthClientSetAuthMethodNegRole(IscsiAuthClient *, int);
    extern int iscsiAuthClientSetChapAlgorithmList(IscsiAuthClient *,
						   unsigned int, const int *);
    extern int iscsiAuthClientSetUsername(IscsiAuthClient *, const char *);
    extern int iscsiAuthClientSetPassword(IscsiAuthClient *,
					  const unsigned char *, unsigned int);
    extern int iscsiAuthClientSetAuthRemote(IscsiAuthClient *, int);
    extern int iscsiAuthClientSetGlueHandle(IscsiAuthClient *, void *);
    extern int iscsiAuthClientSetMethodListName(IscsiAuthClient *,
						const char *);
    extern int iscsiAuthClientSetIpSec(IscsiAuthClient *, int);
    extern int iscsiAuthClientSetBase64(IscsiAuthClient *, int);
    extern int iscsiAuthClientSetChapChallengeLength(IscsiAuthClient *,
						     unsigned int);
    extern int iscsiAuthClientSetVersion(IscsiAuthClient *, int);
    extern int iscsiAuthClientCheckPasswordNeeded(IscsiAuthClient *, int *);

    extern int iscsiAuthClientGetAuthPhase(IscsiAuthClient *, int *);
    extern int iscsiAuthClientGetAuthStatus(IscsiAuthClient *, int *);
    extern int iscsiAuthClientAuthStatusPass(int);
    extern int iscsiAuthClientGetAuthMethod(IscsiAuthClient *, int *);
    extern int iscsiAuthClientGetChapAlgorithm(IscsiAuthClient *, int *);
    extern int iscsiAuthClientGetChapUsername(IscsiAuthClient *, char *,
					      unsigned int);

    extern int iscsiAuthClientSendStatusCode(IscsiAuthClient *, int *);
    extern int iscsiAuthClientGetDebugStatus(IscsiAuthClient *, int *);
    extern const char *iscsiAuthClientDebugStatusToText(int);

/*
 * The following is called by platform dependent code.
 */
    extern void iscsiAuthClientAuthResponse(IscsiAuthClient *, int);

/*
 * The following routines are considered platform dependent,
 * and need to be implemented for use by iscsiAuthClient.c.
 */

    extern int iscsiAuthClientChapAuthRequest(IscsiAuthClient *, char *,
					      unsigned int, unsigned char *,
					      unsigned int, unsigned char *,
					      unsigned int);
    extern void iscsiAuthClientChapAuthCancel(IscsiAuthClient *);

    extern int iscsiAuthClientTextToNumber(const char *, unsigned long *);
    extern void iscsiAuthClientNumberToText(unsigned long, char *,
					    unsigned int);

    extern void iscsiAuthRandomSetData(unsigned char *, unsigned int);
    extern void iscsiAuthMd5Init(IscsiAuthMd5Context *);
    extern void iscsiAuthMd5Update(IscsiAuthMd5Context *, unsigned char *,
				   unsigned int);
    extern void iscsiAuthMd5Final(unsigned char *, IscsiAuthMd5Context *);

    extern int iscsiAuthClientData(unsigned char *, unsigned int *,
				   unsigned char *, unsigned int);

#ifdef __cplusplus
}
#endif
#endif				/* #ifndef ISCSIAUTHCLIENT_H */
