/***********************************************************************
   Note :  This file has been edited with a tab spacing of 4 spaces
 ***********************************************************************

		Project Code    :       AMI-MegaRac

		File Name       :       FTP.H

		Author          :       Parts and Prabhu

		Date            :       Sep 24, 1998

		Purpose         :       Defines and data structures for FTP

				/--------------------------\
				|   American Megatrends    |
				|  Copyright (c) 1998,1999 |
				\--------------------------/

************************************************************************/
#ifndef AMI_FTP_H
#define AMI_FTP_H


///////////////////////////////////////////////////////////////////////
//	#defines...
#define MAXDATASIZE	4000

///////////////////////////////////////////////////////////////////////
//	File transfer Command Values	
#define	FTP_CREATE_FILE		0x0101
#define	FTP_FILE_DATA		0x0102
#define	FTP_FILE_DATA_ACK	0x0103
#define	FTP_CLOSE_FILE		0x0104
#define FTP_ABORT_FILE		0x0105

///////////////////////////////////////////////////////////////////////
//	File transfer Error Codes
#define	FTP_ERR_SUCCESS				0x0000
#define	FTP_ERR_FILE_NAME_REQD		0x0001
#define	FTP_ERR_FILE_CREATE_FAILED	0x0002
#define	FTP_ERR_INVALID_SEQUENCE	0x0003
#define FTP_ERR_USER_ABORT			0x0004
#define FTP_ERR_DISK_ERROR			0x0005
#define	FTP_ERR_FILE_CLOSE_FAILED	0x0006
#define FTP_ERR_FTP_ALREADY_ACTIVE	0x0007
#define FTP_ERR_READFILE			0x0008
#define FTP_FILE1_OVER				0x0009

///////////////////////////////////////////////////////////////////////
//	File transfer messages
#define FTP_SEND_CREATE_COMMAND		0x0001
#define FTP_SEND_PREV_COMMAND		0x0002
#define FTP_ABORT_ACK				0x0003
#define FTP_CREATEFILE_SUCCEEDED	0x0004
#define FTP_INVALID_SEQUENCE		0x0005
#define FTP_NEXT_DATA_PKT			0x0006
#define FTP_ABORT_SESSION			0x0007
#define FTP_CLOSE_SESSION			0x0008

///////////////////////////////////////////////////////////////////////
//  Remote Flash Commands
#define REMOTE_FLASH_SESSION			0x0200
#define REMOTE_FLASH_FTP				0x0201
#define REMOTE_FLASH_START				0x0202
#define REMOTE_FLASH_OVER				0x0203
#define REMOTE_FLASH_ABORTED			0x0204
#define REMOTE_FLASH_IN_PROGRESS		0x0205
#define REMOTE_FLASH_STATUS				0x0206
#define REMOTE_FLASH_BAD_IMAGE			0x0207
#define REMOTE_FLASH_OLD_IMAGE			0x0208
#define REMOTE_FLASH_OVERWRITE_YES		0x0209
#define REMOTE_FLASH_CANCEL				0x020a
#define REMOTE_FLASH_DISCONNECT_NOW		0x020b			// Not used now .... may be used later

///////////////////////////////////////////////////////////////////////
//  Remote Flash messages			
#define REMOTE_FLASH_IS_HOST_READY		0x0001			// Not used now 

///////////////////////////////////////////////////////////////////////
//  Remote Flash Error Codes
#define REMOTE_FLASH_ERR_SUCCESS		0x0000
#define	REMOTE_FLASH_FILE_NAME_REQD		0x0001
#define	REMOTE_FLASH_FILE_CREATE_FAILED	0x0002
#define	REMOTE_FLASH_INVALID_SEQUENCE	0x0003
#define REMOTE_FLASH_USER_ABORT			0x0004
#define REMOTE_FLASH_DISK_ERROR			0x0005
#define	REMOTE_FLASH_FILE_CLOSE_FAILED	0x0006
#define REMOTE_FLASH_FTP_ALREADY_ACTIVE	0x0007

///////////////////////////////////////////////////////////////////////
//  SDK Flash Messages
#define REMOTE_SDK_SESSION				0x0300
#define REMOTE_SDK_FTP					0x0301
#define REMOTE_SDK_START				0x0302
#define REMOTE_SDK_OVER					0x0303
#define REMOTE_SDK_ABORTED				0x0304
#define REMOTE_SDK_IN_PROGRESS			0x0305
#define REMOTE_SDK_STATUS				0x0306
#define REMOTE_SDK_BAD_IMAGE			0x0307
#define REMOTE_SDK_OLD_IMAGE			0x0308
#define REMOTE_SDK_OVERWRITE_YES		0x0309
#define REMOTE_SDK_CANCEL				0x030a

///////////////////////////////////////////////////////////////////////
//	REMOTE FLASH FIRMWARE AND SDK FILE ALSO
#define REMOTE_FLASH_SDK_FTP			0x0400

///////////////////////////////////////////////////////////////////////
//	typedefs...
typedef	struct
{
	u16	Command;
	u16	Status;
	u16	Length;
	u8	Reserved[10];	// For future use
} PTHeader;		// Passthru header

typedef struct
{
	PTHeader	PTH;
	u8		FName [80];
	u32		FileSize;
} CreateFilePacket;

typedef struct
{
	PTHeader	PTH;
	u32		FileOffset;
	u16		Length;
	u8		Data [MAXDATASIZE];
} FileDataPacket;

typedef struct
{
	PTHeader	PTH;
	u32		FileOffset;
} FileDataAckPacket;


typedef struct
{
	PTHeader	PTH;
} CloseFilePacket;

typedef struct
{
	PTHeader	PTH;
	u32		FlashStatus;				// Extended status. number of bytes written. returned by the DLL
	u32		CurrentOp;					// Current Operation like WRITE , READ , ERASE
} RemoteFlashStatusPacket;

typedef struct
{
	PTHeader	PTH;
} AbortFilePacket;

typedef struct
{
	RCS_COMMAND_PACKET		RCSCmdPkt;
	CreateFilePacket	CreateFilePkt;
} RCSReadCreateFilePacket;

typedef struct
{
	RCS_COMMAND_PACKET		RCSCmdPkt;
	CloseFilePacket		CloseFilePkt;
} RCSReadCloseFilePacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	FileDataPacket	FileDataPkt;
} RCSReadFileDataPacket;

typedef struct
{
	RCS_COMMAND_PACKET		RCSCmdPkt;
	FileDataAckPacket	FileDataAckPkt;
} RCSFileDataAckPacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	CloseFilePacket	CloseFilePkt;
} RCSCloseFilePacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	AbortFilePacket	AbortFilePkt;
} RCSAbortFilePacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	PTHeader		PTH;
} RCSStatusToRemotePacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	PTHeader		PTH;
} RCSReadRemoteFlashPacket;

typedef struct
{
	RCS_COMMAND_PACKET	RCSCmdPkt;
	RemoteFlashStatusPacket		RemoteFlashStatusPkt;
} RCSRemoteFlashStatusPacket;


#endif
