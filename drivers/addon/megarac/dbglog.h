/**********************************************************************

		FILE        :       DEBUG.H

		PURPOSE     :       Defines values and structures used for
							checkpoint logging

		AUTHOR(s)   :       Srikumar and Raja

		DATE        :       24th September 1998

		MODIFICATION HISTORY    :


**********************************************************************/
#ifndef __DBGLOG_H__
#define __DBGLOG_H__

#include "dbg_fn.h"

#define COMMON_FOURBYTES  unsigned long
#define LENGTH 524288   //2 megabytes totally


/* error codes */
#define DL_SUCCESS  1
#define DL_ERROR_INDEX_OUT_OF_RANGE -1
#define DL_NOT_LOGGED               -2

/*Severities*/
#define DL_SEV_FATAL   0x00
#define DL_SEV_WARNING 0x01
#define DL_SEV_INFO    0x02
#define DL_SEV_DATA    0x03

#define HEADER_OK 1
#define LOG_OK 2

#define HEADER_CORRUPT 5
#define LOG_CORRUPT 6

/* Checkpoint String Limits */
#define MAX_CHECKPOINT_STRING_LENGTH      80
#define MAX_CHECKPOINT_STRINGS_PER_MODULE 50

typedef struct _CheckPtLogHeader
{
        COMMON_FOURBYTES  head;
        COMMON_FOURBYTES* LogTable;
        COMMON_FOURBYTES  TotalNoOfElementsNow;
        COMMON_FOURBYTES  MaximumNoOfElements;
        COMMON_FOURBYTES  HeaderChecksum;
        COMMON_FOURBYTES  Reserved1;
        COMMON_FOURBYTES  Reserved2;
} CHECKPT_LOG_HEADER;



typedef union _CheckPt
{
        COMMON_FOURBYTES Code;
	struct _DetailedCode
	{
		unsigned int CodeValue:16;
                unsigned int DataLength:6;
                unsigned int Severity:2;
		unsigned int FileDesc:8;
	} DetailedCode;

} CHECKPT;


#define HEADER_LOCATION (CHECKPT_LOG_HEADER*)0x800000
#define TABLE_LOCATION  0x800200
#define CHECKSUM_TABLE_LOCATION  (unsigned short*)TABLE_LOCATION + LENGTH + 10



int BuildCheckpointHeader(void);

int LogCheckpointLong(COMMON_FOURBYTES CheckpointValue);
int LogCheckpoint_FSC(char filedesc, char severity, short codevalue);
int LogCheckpointData(char filedesc, char buffer[], COMMON_FOURBYTES NoOfElements);

int GetCheckpointDump(long index, COMMON_FOURBYTES TotalNoOfElementsRqd, COMMON_FOURBYTES buffer[]);
int GetLast_N_Checkpoint(COMMON_FOURBYTES TotalNoOfElementsRqd, COMMON_FOURBYTES buffer[]);

void GetCheckpointHeader(CHECKPT_LOG_HEADER* FillUp);

void ClearCheckpointLog(void);


#endif
